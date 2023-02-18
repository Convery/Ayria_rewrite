/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-31
    License: MIT
*/

#include <Ayria.hpp>

// Internal access to the database.
namespace Backend::Database
{
    static Hashmap<uint32_t, std::vector<Bytebuffer_t>> Modifiedrows{};
    static Hashmap<uint32_t, std::vector<Bytebuffer_t>> Deletedrows{};
    static Hashmap<uint32_t, Hashset<Callback_t>> onModifiedCB{};

    // For debugging.
    static void SQLErrorlog(void *DBName, int Errorcode, const char *Errorstring)
    {
        (void)DBName; (void)Errorcode; (void)Errorstring;
        Debugprint(va("SQL error %i in %s: %s", Errorcode, DBName, Errorstring));
    }

    // On changes to the database.
    static void Clientupdatehook(void *, sqlite3 *DB, int Operation, const char *, const char *Table, int64_t, int64_t)
    {
        // Ignore internal tables.
        const auto Tablehash = Hash::WW32(std::string(Table));
        if (Tablehash == Hash::WW32("Syncpacket") || Tablehash == Hash::WW32("Account"))
            return;

        // Which interface is the relevant one.
        const auto getValue = (Operation == SQLITE_DELETE) ? sqlite3_preupdate_old : sqlite3_preupdate_new;

        // Can probably be optimized out as we check the return value anyways.
        const auto Columns = sqlite3_preupdate_count(DB);

        // Fetch all columns.
        Bytebuffer_t Tabledata{};
        for (int i = 0; i < Columns; ++i)
        {
            sqlite3_value *Value{};
            if (SQLITE_OK != getValue(DB, i, &Value))
                break;

            switch (sqlite3_value_type(Value))
            {
                case SQLITE_TEXT: { Tabledata << std::u8string((const char8_t *)sqlite3_value_text(Value)); break; }
                case SQLITE_INTEGER: { Tabledata << sqlite3_value_int64(Value); break; }
                case SQLITE_FLOAT: { Tabledata << sqlite3_value_double(Value); break; }
                case SQLITE_NULL: { Tabledata.WriteNULL(); break; }
                case SQLITE_BLOB:
                {
                    const auto Size = sqlite3_value_bytes(Value);
                    const auto Data = sqlite3_value_blob(Value);

                    Tabledata << std::span<const uint8_t>((const uint8_t *)Data, Size);
                    break;
                }
            }
        }

        if (Operation == SQLITE_DELETE)
            Deletedrows[Tablehash].emplace_back(Tabledata);
        else
            Modifiedrows[Tablehash].emplace_back(Tabledata);
    }

    // Callbacks on database modification.
    void Register(uint32_t TableID, Callback_t Callback)
    {
        onModifiedCB[TableID].insert(Callback);
    }

    // Database setup and cleanup.
    static std::shared_ptr<sqlite3> DBConnection{};
    static void InitializeDB()
    {
        const sqlite::Database_t Database(DBConnection);

        // Database configuration.
        Database << "PRAGMA foreign_keys = ON;";
        Database << "PRAGMA temp_store = MEMORY;";
        Database << "PRAGMA auto_vacuum = INCREMENTAL;";

        // Helper functions for inline hashing.
        {
            static constexpr auto Lambda32 = [](sqlite3_context *context, int argc, sqlite3_value **argv) -> void
            {
                if (argc == 0) return;
                if (SQLITE3_TEXT != sqlite3_value_type(argv[0])) { sqlite3_result_null(context); return; }

                // SQLite may invalidate the pointer if _bytes is called after text.
                const auto Length = sqlite3_value_bytes(argv[0]);
                const auto Hash = Hash::WW32(sqlite3_value_text(argv[0]), Length);
                sqlite3_result_int(context, Hash);
            };
            static constexpr auto Lambda64 = [](sqlite3_context *context, int argc, sqlite3_value **argv) -> void
            {
                if (argc == 0) return;
                if (SQLITE3_TEXT != sqlite3_value_type(argv[0])) { sqlite3_result_null(context); return; }

                // SQLite may invalidate the pointer if _bytes is called after text.
                const auto Length = sqlite3_value_bytes(argv[0]);
                const auto Hash = Hash::WW64(sqlite3_value_text(argv[0]), Length);
                sqlite3_result_int64(context, Hash);
            };
            static constexpr auto Lambda = [](sqlite3_context *context, int argc, sqlite3_value **argv) -> void
            {
                if (argc == 0) return;
                if (SQLITE3_TEXT != sqlite3_value_type(argv[0])) { sqlite3_result_null(context); return; }

                // SQLite may invalidate the pointer if _bytes is called after text.
                const auto Length = sqlite3_value_bytes(argv[0]);
                const std::u8string Text((char8_t *)sqlite3_value_text(argv[0]), Length);

                sqlite3_result_int64(context, (Hash::WW64(Text) << 32) | Hash::WW32(Text));
            };

            sqlite3_create_function(Database.Connection.get(), "WW32", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS, nullptr, Lambda32, nullptr, nullptr);
            sqlite3_create_function(Database.Connection.get(), "WW64", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS, nullptr, Lambda64, nullptr, nullptr);
            sqlite3_create_function(Database.Connection.get(), "ShortID", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS, nullptr, Lambda, nullptr, nullptr);
        }

        // All tables depend on the account as primary identifier.
        constexpr auto Account =
            "CREATE TABLE IF NOT EXISTS Account ("
            "Publickey TEXT PRIMARY KEY, "
            "Firstseen INTEGER, "
            "Lastseen INTEGER, "
            "ShortID INTEGER );";
        Database << Account;
    }
    static void CleanupDB()
    {
        const sqlite::Database_t Database(DBConnection);

        Database << "PRAGMA incremental_vacuum;";
        Database << "PRAGMA optimize;";
    }

    // Open the database for writing.
    sqlite::Database_t Open()
    {
        if (!DBConnection) [[unlikely]]
        {
            sqlite3 *Ptr{};

            // :memory: should never fail unless the client has more serious problems.
            auto Result = sqlite3_open_v2("./Ayria/Client.sqlite", &Ptr, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
            if (Result != SQLITE_OK) Result = sqlite3_open_v2(":memory:", &Ptr, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
            assert(Result == SQLITE_OK);

            // Log errors in debug-mode.
            if constexpr (Build::isDebug) sqlite3_db_config(Ptr, SQLITE_CONFIG_LOG, SQLErrorlog, "Client.sqlite");

            // Track our changes to the DB.
            sqlite3_preupdate_hook(Ptr, Clientupdatehook, nullptr);

            // Close the DB at exit to ensure everything's flushed.
            DBConnection = std::shared_ptr<sqlite3>(Ptr, [](sqlite3 *Ptr) { sqlite3_close_v2(Ptr); });

            // Cleanup the DB on exit.
            (void)std::atexit(CleanupDB);
            InitializeDB();
        }

        return sqlite::Database_t(DBConnection);
    }

    // Poll for updates every 50ms.
    static void __cdecl Poll()
    {
        Hashmap<uint32_t, std::vector<Bytebuffer_t>> Modified{};
        Hashmap<uint32_t, std::vector<Bytebuffer_t>> Deleted{};
        Modifiedrows.swap(Modified);
        Deletedrows.swap(Deleted);

        // Pump to any interested handlers.
        for (const auto &[Type, Rows] : Deleted)
        {
            if (onModifiedCB.contains(Type))
            {
                for (const auto &Row : Rows)
                {
                    for (const auto &Callback : onModifiedCB[Type])
                    {
                        Callback(true, Row);
                    }
                }
            }
        }
        for (const auto &[Type, Rows] : Modified)
        {
            if (onModifiedCB.contains(Type))
            {
                for (const auto &Row : Rows)
                {
                    for (const auto &Callback : onModifiedCB[Type])
                    {
                        Callback(false, Row);
                    }
                }
            }
        }
    }

    // Register background task.
    struct Startup_t { Startup_t() { Backgroundtasks::addPeriodictask(Poll, 50); } } Startup{};
}
