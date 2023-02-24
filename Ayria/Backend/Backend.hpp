/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-11-09
    License: MIT
*/

#pragma once
#include <Ayria.hpp>

// Simple callback system that runs periodically from a background thread.
namespace Backend::Backgroundtasks
{
    using Callback_t = void(__cdecl *)();

    // Startup tasks are only ran once, at Initialize().
    void addPeriodictask(Callback_t Callback, uint32_t PeriodMS = 1000);
    void addStartuptask(Callback_t Callback);

    // Called from usercode (in or after main).
    void Initialize();
    void Terminate();
}

// Helper to add packets from / to different sources.
namespace Backend::Synchronization
{
    using Callback_t = void(__cdecl *)(const qDSA::Publickey_t &Publickey, int64_t RowID, int64_t Timestamp, const Bytebuffer_t &Payload);

    // Fetch inserted messages.
    Hashset<int64_t> getMessagerows();

    // Parse a message and insert into the client row.
    void Register(uint32_t Messagetype, Callback_t Callback);
    inline void Register(std::string_view Messagetype, Callback_t Callback)
    {
        return Register(Hash::WW32(Messagetype), Callback);
    }

    // Create and insert messages into the database.
    Blob_t Createmessage(uint32_t Messagetype, const Bytebuffer_t &Payload);
    inline Blob_t Createmessage(std::string_view Messagetype, const Bytebuffer_t &Payload) { return Createmessage(Hash::WW32(Messagetype), Payload); }
    void Storemessage(const qDSA::Signature_t &Signature, const qDSA::Publickey_t &Publickey, uint32_t Messagetype, int64_t Timestamp, const Bytebuffer_t &Payload);
}

// Internal access to the database.
namespace Backend::Database
{
    using Callback_t = void(__cdecl *)(bool isDeleted, const Bytebuffer_t &Tabledata);

    // Callbacks on database modification.
    void Register(uint32_t TableID, Callback_t Callback);
    inline void Register(std::string_view Tablename, Callback_t Callback)
    {
        return Register(Hash::WW32(Tablename), Callback);
    }

    // Open the database for writing.
    sqlite::Database_t Open();
}

// Handle networking in the background.
namespace Backend::Network
{
    // 108 bytes + payload on line.
    #pragma pack(push, 1)
    struct Header_t
    {
        std::array<uint8_t, 64> Signature;
        std::array<uint8_t, 32> Publickey;

        // VVV Singned data VVV
        uint32_t Messagetype;
        int64_t Timestamp;
    };
    #pragma pack(pop)

    // Resolve the clients IP.
    inline uint32_t getInternalIP() { return {}; }
    inline uint32_t getExternalIP() { return {}; }

    // Publish a payload to the network.
    void PublishLAN(const Blob_t &Packet, bool Delayed = false);
    inline void PublishWAN(const Blob_t &Packet, bool Delayed = false) {}

    // For internal use.
    inline void Publish(const Blob_t &Packet, bool Delayed = false)
    {
        PublishLAN(Packet, Delayed);
        PublishWAN(Packet, Delayed);
    }
}

// Load the the configuration from disk.
namespace Backend::Config
{
    // Fills Globalstate_t from ./Ayria/Config.json
    void Load();

    // Helper to set the publickey.
    void setPublickey(std::u8string_view CredentialA, std::u8string_view CredentialB);
    void setPublickey_HWID();
    void setPublickey_RNG();
}

// User and plugin interaction with the backend.
namespace Backend::Plugins
{
    // Different types of hooking.
    bool InstallTLSHook();
    bool InstallEPHook();

    // Broadcast a message to all plugins.
    void Broadcast(uint32_t MessageID, const std::string& JSONString);
    inline void Broadcast(std::string_view Message, const std::string &JSONString)
    {
        return Broadcast(Hash::WW32(Message), JSONString);
    }

    // Simply load all plugins from disk.
    void Initialize();
}

// Helpers for common actions.
namespace Backend
{
    // Tasks are generally periodic.
    inline void Enqueuetask(Backgroundtasks::Callback_t Callback, uint32_t PeriodMS = 1000)
    {
        Backgroundtasks::addPeriodictask(Callback, PeriodMS);
    }

    // Evaluation of the prepared statement via >> operator or dtor.
    template <typename ...Args> [[nodiscard]] auto Query(std::string_view SQL, Args&&... va)
    {
        auto PS = Database::Open() << SQL;
        if constexpr (sizeof...(va) > 0)
        {
            ((PS << va), ...);
        }
        return PS;
    }
}
