/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-14
    License: MIT
*/

#include <Ayria.hpp>
#include "Communication.hpp"

// Generally created by services.
namespace Communication::Notifications
{
    static Hashmap<uint32_t, Hashset<CPPCallback_t>> CPPNotifications{};
    static Hashmap<uint32_t, Hashset<CCallback_t>> CNotifications{};

    // Duplicate subscriptions get ignored.
    void Subscribe(std::string_view Identifier, CCallback_t Handler)
    {
        CNotifications[Hash::WW32(Identifier)].insert(Handler);
    }
    void Subscribe(std::string_view Identifier, CPPCallback_t Handler)
    {
        CPPNotifications[Hash::WW32(Identifier)].insert(Handler);
    }
    void Unsubscribe(std::string_view Identifier, CCallback_t Handler)
    {
        CNotifications[Hash::WW32(Identifier)].erase(Handler);
    }
    void Unsubscribe(std::string_view Identifier, CPPCallback_t Handler)
    {
        CPPNotifications[Hash::WW32(Identifier)].erase(Handler);
    }

    // RowID is the row in Syncpacket that triggered the notification.
    void Publish(std::string_view Identifier, int64_t RowID, JSON::Value_t &&Payload)
    {
        const auto ID = Hash::WW32(Identifier);
        const auto C = Payload.dump();
        const auto &CPP = Payload;

        if (CNotifications.contains(ID)) [[likely]]
            for (const auto &Handler : CNotifications[ID])
                Handler(RowID, C.data(), uint32_t(C.size()));

        if (CPPNotifications.contains(ID)) [[likely]]
            for (const auto &Handler : CPPNotifications[ID])
                Handler(RowID, CPP);
    }
    void Publish(std::string_view Identifier, int64_t RowID, std::string_view Payload)
    {
        const auto ID = Hash::WW32(Identifier);
        const auto CPP = JSON::Parse(Payload);
        const auto &C = Payload;

        if (CNotifications.contains(ID)) [[likely]]
            for (const auto &Handler : CNotifications[ID])
                Handler(RowID, C.data(), uint32_t(C.size()));

        if (CPPNotifications.contains(ID)) [[likely]]
            for (const auto &Handler : CPPNotifications[ID])
                Handler(RowID, CPP);
    }

    // Access from the plugins.
    namespace Export
    {
        extern "C" EXPORT_ATTR void __cdecl unsubscribeNotifications(const char *Identifier, void(__cdecl *Callback)(int64_t RowID, const char *Payload, uint32_t Length))
        {
            if (!Identifier || !Callback) [[unlikely]]
            {
                assert(false);
                return;
            }

            Unsubscribe(Identifier, Callback);
        }
        extern "C" EXPORT_ATTR void __cdecl subscribeNotifications(const char *Identifier, void(__cdecl *Callback)(int64_t RowID, const char *Payload, uint32_t Length))
        {
            if (!Identifier || !Callback) [[unlikely]]
            {
                assert(false);
                return;
            }

            Subscribe(Identifier, Callback);
        }
        extern "C" EXPORT_ATTR void __cdecl publishNotification(const char *Identifier, const char *Payload, uint32_t Length)
        {
            if (!Identifier || !Payload) [[unlikely]]
            {
                assert(false);
                return;
            }

            // Use -1 as a placeholder for outside messaging.
            Publish(Identifier, -1, std::string_view{ Payload, Length });
        }
    }
}
