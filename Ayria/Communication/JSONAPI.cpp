/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-12
    License: MIT
*/

#include <Ayria.hpp>
#include "Communication.hpp"

namespace Communication::JSONAPI
{
    // NOTE(tcn): Increase this value in the future.
    static constexpr size_t Backlog = 16;

    // We return the response by char *, they exist for N responses.
    static Hashmap<std::string, Callback_t> Requesthandlers{};
    static Ringbuffer_t<std::string, Backlog> Results{};

    // Rather than adding generic responses to the buffer.
    constexpr auto Generichash = Hash::WW32("{}");
    constexpr auto Genericresponse = "{}";

    // Listen for requests to this functionname.
    void addEndpoint(std::string_view Functionname, Callback_t Callback)
    {
        assert(Callback);
        assert(!Functionname.empty());

        Requesthandlers[std::string{ Functionname }] = Callback;
    }

    // Access from the plugins.
    extern "C" EXPORT_ATTR const char *__cdecl JSONRequest(const char *Endpoint, const char *JSONString)
    {
        // Rather than asserting on NULL, fall-through to the error handler.
        const std::string Functionname = Endpoint ? Endpoint : "";

        // Return a list of available endpoints if misspelled.
        if (!Requesthandlers.contains(Functionname)) [[unlikely]]
        {
            static std::string Failurestring = va(R"({ "Error" : "No endpoint with name \"%.*s\" available.", \n"Endpoints" : [ )",
                                                  Functionname.size(), Functionname.data());

            for (const auto &Name : Requesthandlers | std::views::keys)
            {
                Failurestring.append(Name);
                Failurestring.append(",");
            }

            Failurestring.pop_back();
            Failurestring.append(R"( ] })");
            return Failurestring.c_str();
        }

        // Subscript operator prefered due to nothrow.
        const auto Response = Requesthandlers[Functionname](JSON::Parse(JSONString));

        // Common case of a function setting data and not returning anything.
        if (Response.empty() || Hash::WW32(Response) == Generichash) [[likely]]
            return Genericresponse;

        // The result exists on the heap for N messages.
        return Results.emplace_back(Response).c_str();
    }
}
