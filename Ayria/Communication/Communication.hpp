/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-12
    License: MIT

    Systems for helping services and plugins communicate.
    All content is encoded in JSON for useability.
*/

#pragma once
#include <Ayria.hpp>

// Generally created by services.
namespace Communication::Notifications
{
    // static void __cdecl CPPCallback(int64_t RowID, const JSON::Object_t &Payload);
    using CPPCallback_t = void(__cdecl *)(int64_t RowID, const JSON::Object_t &Payload);

    // static void __cdecl CCallback(int64_t RowID, const char *Payload, uint32_t Length);
    using CCallback_t = void(__cdecl *)(int64_t RowID, const char *Payload, uint32_t Length);

    // Duplicate subscriptions get ignored.
    void Subscribe(std::string_view Identifier, CCallback_t Handler);
    void Subscribe(std::string_view Identifier, CPPCallback_t Handler);
    void Unsubscribe(std::string_view Identifier, CCallback_t Handler);
    void Unsubscribe(std::string_view Identifier, CPPCallback_t Handler);

    // RowID is the row in Syncpacket that triggered the notification.
    void Publish(std::string_view Identifier, int64_t RowID, JSON::Value_t &&Payload);
    void Publish(std::string_view Identifier, int64_t RowID, std::string_view Payload);
}

// Generally called from plugins, basic request-response.
namespace Communication::JSONAPI
{
    // static std::string __cdecl Callback(JSON::Value_t &&Request);
    using Callback_t = std::string(__cdecl *)(JSON::Value_t &&Request);

    // Listen for requests to this functionname.
    void addEndpoint(std::string_view Functionname, Callback_t Callback);
}

// Primarilly for user interaction, but can be called from plugins as well.
namespace Communication::Console
{
    // UTF8 escaped ASCII strings are passed to argv for compatibility with C-plugins.
    using Functioncallback_t = void(__cdecl *)(int argc, const char **argv);
    using Logline_t = std::pair<std::u8string, uint32_t>;

    // Useful for checking for new messages.
    extern std::atomic<uint32_t> LastmessageID;

    // Threadsafe injection and fetching from the global log.
    void addMessage(Logline_t &&Message);
    std::vector<Logline_t> getMessages(size_t Maxcount = 256, std::u8string_view Filter = {});
    inline void addMessage(std::u8string_view Message, uint32_t ARGBColor = 0) { return addMessage({ std::u8string{Message}, ARGBColor }); }

    // Manage and execute the commandline, with optional logging.
    void execCommand(std::u8string_view Commandline, bool Log = true);
    void addCommand(std::u8string_view Name, Functioncallback_t Callback);

    // Helpers for internal conversion.
    template <typename T> inline void execCommand(std::basic_string_view<T> Commandline, bool Log = true)
    {
        // Passthrough for T = char8_t
        return execCommand(Encoding::toUTF8(Commandline), Log);
    }
    template <typename T> inline void addMessage(std::basic_string_view<T> Message, uint32_t ARGBColor = 0)
    {
        // Passthrough for T = char8_t
        return addMessage({ Encoding::toUTF8(Message), ARGBColor });
    }
    template <typename T> inline void addCommand(std::basic_string_view<T> Name, Functioncallback_t Callback)
    {
        // Passthrough for T = char8_t
        return addCommand(Encoding::toUTF8(Name), Callback);
    }
}
