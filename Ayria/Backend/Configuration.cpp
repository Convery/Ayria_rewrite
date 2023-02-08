/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-02
    License: MIT
*/

#include <Ayria.hpp>

namespace Backend::Config
{
    static std::string Configpath = "./Ayria/Config.json";

    // Save the configuration to disk.
    static void Saveconfig()
    {
        JSON::Object_t Object{};

        // Need to cast the bits to bool as otherwise they'd be uint16.
        Object[u8"enableExternalconsole"] = (bool)Global.Configuration.enableExternalconsole;
        Object[u8"enableIATHooking"] = (bool)Global.Configuration.enableIATHooking;
        Object[u8"enableFileshare"] = (bool)Global.Configuration.enableFileshare;
        Object[u8"noNetworking"] = (bool)Global.Configuration.noNetworking;
        Object[u8"pruneDB"] = (bool)Global.Configuration.pruneDB;
        Object[u8"Username"] = *Global.Username;

        FS::Writefile(Configpath, JSON::Dump(Object));
    }

    // Fills Globalstate_t
    void Load()
    {
        // Load the last configuration from disk.
        const auto Config = JSON::Parse(FS::Readfile<char8_t>(Configpath));
        Global.Configuration.enableExternalconsole = Config.value<bool>("enableExternalconsole");
        Global.Configuration.enableIATHooking = Config.value<bool>("enableIATHooking");
        Global.Configuration.enableFileshare = Config.value<bool>("enableFileshare");
        Global.Configuration.noNetworking = Config.value<bool>("noNetworking");
        Global.Configuration.pruneDB = Config.value<bool>("pruneDB");
        *Global.Username = Config.value(u8"Username", u8"AYRIA"s);

        // Select a source for crypto..
        if (std::strstr(GetCommandLineA(), "--randID"))
        {
            PK_Random();
        }
        else
        {
            PK_byHWID();
        }

        // Notify the user about the current settings.
        Infoprint("Loaded account:");
        Infoprint(va("ShortID: 0x%08X", Global.getShortID()));
        Infoprint(va("LongID: %s", Global.getLongID().c_str()));
        Infoprint(va("Username: %s", *Global.Username));

        // If there was no config, force-save one for the user instantly.
        (void)std::atexit([]() { if (Global.Configuration.modifiedConfig) Saveconfig(); });
        if (Config.empty()) Saveconfig();
    }

    // Just two random sources for somewhat static data.
    inline cmp::Array_t<uint8_t, 32> bySMBIOS()
    {
        uint8_t Version_major{};
        std::u8string_view Table;

        #if defined (_WIN32)
        const auto Size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
        const auto Buffer = std::make_unique<char8_t[]>(Size);
        GetSystemFirmwareTable('RSMB', 0, Buffer.get(), Size);

        Version_major = *(uint8_t *)(Buffer.get() + 1);
        const auto Tablelength = *(uint32_t *)(Buffer.get() + 4);
        Table = std::u8string_view(Buffer.get() + 8, Tablelength);

        #else // Linux assumed.
        const auto File = FS::Readfile<char8_t>("/sys/firmware/dmi/tables/smbios_entry_point");

        // SMBIOS
        if (*(uint32_t *)File.data() == 0x5F534D5F)
        {
            Version_major = File[6];

            const auto Offset = *(uint32_t *)(File.data() + 0x18);
            const auto Tablelength = *(uint32_t *)(File.data() + 0x16);
            Table = std::u8string_view(File.data() + Offset, Tablelength);
        }

        // SMBIOS3
        if (*(uint32_t *)File.data() == 0x5F534D33)
        {
            Version_major = File[7];

            const auto Offset = *(uint64_t *)(File.data() + 0x10);
            const auto Tablelength = *(uint32_t *)(File.data() + 0x0C);
            Table = std::u8string_view(File.data() + Offset, Tablelength);
        }
        #endif

        // Sometimes 2.x is reported as "default" AKA 0.
        if (Version_major == 0 || Version_major >= 2)
        {
            std::vector<std::string> Serials{};

            while (!Table.empty())
            {
                // Helper to skip trailing strings.
                const auto Structsize = [](const char8_t *Start) -> size_t
                {
                    auto End = Start;
                    End += Start[1];

                    if (!*End) End++;
                    while (*(End++)) while (*(End++)) {};
                    return End - Start;
                } (Table.data());

                auto Entry = Table.substr(0, Structsize);
                const auto Headerlength = Entry[1];
                const auto Type = Entry[0];

                // Sometimes messed up if using modded BIOS, i.e. 02000300040005000006000700080009
                if (Type == 1)
                {
                    std::string Serial; Serial.reserve(16);

                    for (size_t i = 0; i < 16; ++i) Serial.append(std::format("{:02X}", (uint8_t)Entry[8 + i]));

                    Serials.push_back(Serial);
                }

                // Sometimes not actually filled..
                if (Type == 2)
                {
                    const auto Stringindex = Entry[0x07];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    Serials.push_back((char *)Entry.data());
                }

                // Only relevant for laptops.
                if (Type == 3)
                {
                    const auto Stringindex = Entry[0x06];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    Serials.push_back((char *)Entry.data());
                }

                // Not as unique as we want..
                if (Type == 4)
                {
                    const auto Serial = std::format("{}", *(uint64_t *)&Entry[8]);
                    Serials.push_back(Serial);
                }

                // Some laptops do not have a tag.
                if (Type == 17)
                {
                    const auto Stringindex = Entry[0x18];
                    Entry.remove_prefix(Headerlength);

                    for (uint8_t i = 1; i < Stringindex; ++i)
                        Entry.remove_prefix(std::strlen((char *)Entry.data()) + 1);

                    Serials.push_back((char *)Entry.data());
                }
                Table.remove_prefix(Structsize);
            }

            // Filter out bad OEM strings.
            auto Baseview = Serials
                | std::views::transform([](std::string &String) { for (auto &Char : String) Char = std::toupper(Char); return String; })
                | std::views::filter([](const std::string &String) -> bool
                {
                    const std::vector<std::string> Badstrings = { "NONE", "FILLED", "OEM", "O.E.M.", "00020003000400050006000700080009", "SERNUM" };

                    if (String.empty()) return false;
                    return std::ranges::none_of(Badstrings, [&](const auto &Item) { return std::strstr(String.c_str(), Item.c_str()); });
                });

            // C++ does not have ranges::actions yet.
            auto Unique = std::vector(Baseview.begin(), Baseview.end()); std::ranges::sort(Unique);
            const auto [First, Last] = std::ranges::unique(Unique);
            Unique.erase(First, Last);

            // Limit ourselves to 3 items in-case of changes.
            auto View = Unique | std::views::take(3);

            // In-case we want to verify something.
            if constexpr (Build::isDebug)
            {
                for (const auto &Item : View)
                    Infoprint(va("Serial: %s", Item));
            }

            // Mix the serials so that the order doesn't matter.
            size_t Longest = 0;
            for (const auto &Item : View)
                Longest = std::max(Longest, Item.size());

            const auto Mixbuffer = (uint8_t *)alloca(Longest);
            std::memset(Mixbuffer, 0, Longest);

            for (const auto &Item : View)
                for (size_t i = 0; i < Item.size(); ++i)
                    Mixbuffer[i] ^= Item[i];

            return Hash::SHA256(std::span(Mixbuffer, Longest));
        }

        return {};
    }
    inline cmp::Array_t<uint8_t, 32> byDisk()
    {
        constexpr auto getNVME = []() -> std::string
        {
            std::array<uint32_t, 1036> Query = { 49, 0, 3, 1, 1, 0, 40, 4096 };

            const auto Handle = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ | GENERIC_WRITE,
                                                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

            if (Handle == INVALID_HANDLE_VALUE) Warningprint("getNVME requires admin.");

            if (!DeviceIoControl(Handle, 0x2D1400, &Query, sizeof(Query), &Query, sizeof(Query), NULL, NULL))
            {
                CloseHandle(Handle);
                return {};
            }
            else
            {
                // Some manufacturers don't care about endian..
                const auto Buffer = std::span((char *)&Query[13], 20);
                const auto Spaces = std::ranges::count(Buffer, ' ');
                if (Buffer[Buffer.size() - Spaces - 1] == ' ')
                {
                    for (uint8_t i = 0; i < 20; i += 2)
                    {
                        std::swap(Buffer[i], Buffer[i + 1]);
                    }
                }

                CloseHandle(Handle);
                return std::string((char *)&Query[13], 20);
            }
        };
        constexpr auto getSATA = []() -> std::string
        {
            std::array<uint8_t, 548> Query{ 0, 0, 0, 0, 0, 1, 1, 0, 0, 0xA0, 0xEC }; *(uint32_t *)&Query[0] = 512;

            const auto Handle = CreateFileW(L"\\\\.\\PhysicalDrive0", GENERIC_READ | GENERIC_WRITE,
                                                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

            if (Handle == INVALID_HANDLE_VALUE) Warningprint("getSATA requires admin.");

            if (!DeviceIoControl(Handle, 0x0007C088, &Query, sizeof(Query), &Query, sizeof(Query), NULL, NULL))
            {
                CloseHandle(Handle);
                return {};
            }
            else
            {
                // Some manufacturers don't care about endian..
                const auto Buffer = std::span((char *)&Query[36], 20);
                const auto Spaces = std::ranges::count(Buffer, ' ');
                if (Buffer[Buffer.size() - Spaces - 1] == ' ')
                {
                    for (uint8_t i = 0; i < 20; i += 2)
                    {
                        std::swap(Buffer[i], Buffer[i + 1]);
                    }
                }

                CloseHandle(Handle);
                return std::string((char *)&Query[36], 20);
            }
        };

        const auto Serial1 = getNVME();
        if constexpr (Build::isDebug) Infoprint(va("NVME: %s", Serial1));
        if (!Serial1.empty()) return Hash::SHA256(Serial1);

        const auto Serial2 = getSATA();
        if constexpr (Build::isDebug) Infoprint(va("SATA: %s", Serial2));
        if (!Serial2.empty()) return Hash::SHA256(Serial2);

        return {};
    }

    // Helper to set the publickey.
    void PK_byCredential(std::u8string_view A, std::u8string_view B)
    {
        // TODO(tcn): Should probably have a more complex algo..
        const auto Combined = Hash::SHA256(A) + Hash::SHA256(B);
        std::array<uint8_t, 64> Temporary = Hash::SHA512(Combined);

        // Slow things down a bit.
        for (size_t i = 0; i < 1'000; ++i)
            Temporary = Hash::SHA512(Temporary);

        const auto Seed = Hash::SHA512(Hash::SHA256(Temporary) + Hash::SHA256(Combined));
        std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Seed);
    }
    void PK_byHWID()
    {
        // SMBIOS should always be available.
        if (const auto BIOS = bySMBIOS(); !BIOS.empty()) [[likely]]
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(BIOS);
            return;
        }

        // Requires administrator privileges.
        if (const auto DISK = byDisk(); !DISK.empty())
        {
            std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(DISK);
            return;
        }

        Errorprint("Could not generate a fixed public-key, run as admin to save progress.");
        PK_Random();
    }
    void PK_Random()
    {
        std::array<uint32_t, 8> Source;
        rand_s(&Source[0]); rand_s(&Source[1]);
        rand_s(&Source[2]); rand_s(&Source[3]);
        rand_s(&Source[4]); rand_s(&Source[5]);
        rand_s(&Source[6]); rand_s(&Source[7]);

        std::tie(Global.Publickey, *Global.Privatekey) = qDSA::Createkeypair(Hash::SHA512(Source));
    }
}
