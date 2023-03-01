/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-25
    License: MIT
*/

#include <Ayria.hpp>

namespace Backend::Synchronization
{
    static Hashmap<uint32_t, Hashset<Callback_t>> Messagehandlers{};
    static Hashset<int64_t> Modifiedrows{};

    // Create and insert messages into the database.
    Blob_t Createmessage(uint32_t Messagetype, const Bytebuffer_t &Payload)
    {
        Blob_t Packet(sizeof(Network::Header_t) + Payload.size(), 0);
        const auto Header = reinterpret_cast<Network::Header_t *>(Packet.data());
        const auto Signedpart = std::span(Packet.data() + 96, Payload.size() + 12);

        // Timetamp in UTC
        Header->Timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        Header->Publickey = Global.Publickey;
        Header->Messagetype = Messagetype;

        // Signed content.
        std::memcpy(Packet.data() + sizeof(Network::Header_t), Payload.data(), Payload.size());
        Header->Signature = qDSA::Sign(Global.Publickey, *Global.Privatekey, Signedpart);

        // Assume that we are going to send this, and save it.
        Storemessage(Header->Signature, Header->Publickey, Header->Messagetype, Header->Timestamp, Payload);

        return Packet;
    }
    void Storemessage(const qDSA::Signature_t &Signature, const qDSA::Publickey_t &Publickey, uint32_t Messagetype, int64_t Timestamp, const Bytebuffer_t &Payload)
    {
        const std::u8string PK = Base58::Encode(Publickey);

        // Ensure that an account exists for this PK.
        const auto ShortID = (Hash::WW64(Publickey) << 32) | Hash::WW32(Publickey);
        Query("INSERT OR IGNORE INTO Account VALUES (?, ?, ?, ?);", PK, Timestamp, Timestamp, ShortID).Execute();

        // Standard insert.
        auto PS = Query("INSERT INTO Syncpacket VALUES (?, ?, ?, ?, ?) RETURNING rowid;");
        PS << PK << (std::u8string)Base58::Encode(Signature);
        PS << Messagetype << Timestamp;
        PS << Base85::Encode(Payload.as_span());

        // Returning rowid.
        int64_t RowID;
        PS >> RowID;

        // Mark for processing next frame (if not ours).
        if (Publickey != Global.Publickey) Modifiedrows.insert(RowID);

        // Update timestamps.
        int64_t First{}, Last{};
        Query("SELECT Firstseen, Lastseen FROM Account WHERE Publickey = ?;", PK) >> std::tie(First, Last);
        Query("UPDATE Account SET Firstseen = ?, Lastseen = ? WHERE Publickey = ?;", std::min(First, Timestamp), std::max(Last, Timestamp), PK).Execute();
    }

    // Parse a message and insert into the client row.
    void Register(uint32_t Messagetype, Callback_t Callback)
    {
        Messagehandlers[Messagetype].insert(Callback);
    }

    // Check for new inserts every 50ms.
    static void __cdecl Poll()
    {
        Hashset<int64_t> Rows{};
        Modifiedrows.swap(Rows);

        for (const auto Row : Rows)
        {
            Query("SELECT * FROM Syncpacket WHERE rowid = ?;", Row) >> [&](const std::u8string &Publickey, const std::u8string &, uint32_t Messagetype, int64_t Timestamp, const Blob_t &Data)
            {
                if (Messagehandlers.contains(Messagetype))
                {
                    // Known input size.
                    std::array<char8_t, Base58::Encodesize(sizeof(qDSA::Publickey_t))> Fixedsize{};
                    std::ranges::copy(Publickey, Fixedsize.data());

                    const auto Payload = Bytebuffer_t(Base85::Decode(Data));
                    const auto PK = Base58::Decode(Fixedsize);

                    for (const auto Handler : Messagehandlers[Messagetype])
                    {
                        Handler(PK, Row, Timestamp, Data);
                    }
                }
            };
        }
    }

    // Prune the DB on exit.
    static void CleanupDB()
    {
        // Only remove packets if the user wants to.
        if (!!Global.Configuration.pruneDB)
        {
            // Assume all services save a foreign-key reference to rowid's they want preserved..
            const auto Timestamp = (std::chrono::system_clock::now() - std::chrono::hours(24)).time_since_epoch();

            // TODO(tcn): Find a way to "DELETE FROM Syncpackets WHERE (Timestamp < ?)" while ignoring errors.
            std::vector<int64_t> Rows{};
            Query("SELECT rowid FROM Syncpackets WHERE (Timestamp < ?);", Timestamp.count()) >> Rows;
            for (const auto &Row : Rows)
            {
                Query("DELETE FROM Syncpackets WHERE (rowid = ?);", Row).Execute();
            }
        }
    }

    // On startup.
    static void __cdecl Initialize()
    {
        constexpr auto Syncpacket =
            "CREATE TABLE IF NOT EXISTS Syncpacket ("
            "Publickey TEXT REFERENCES Account(Publickey) ON DELETE CASCADE, "
            "Signature TEXT, "

            "Messagetype INTEGER, "
            "Timestamp INTEGER, "

            "Data BLOB,"
            "UNIQUE (Publickey, Signature) );";

        // Set up DB table.
        Query(Syncpacket).Execute();

        // Announce ourselves (and ensure that we exist in the DB).
        Network::Publish(Createmessage("Clientstartup", {}), true);

        // Add periodic tasks.
        Enqueuetask(Poll, 50);

        // Ensure all messages are processed.
        (void)std::atexit(Poll);

        // Remove old syncpackets.
        (void)std::atexit(CleanupDB);
    }

    // Register initialization to run on startup.
    struct Startup_t { Startup_t() { Backgroundtasks::addStartuptask(Initialize); } } Startup{};
}
