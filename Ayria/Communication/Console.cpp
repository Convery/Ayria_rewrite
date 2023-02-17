/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-14
    License: MIT
*/

#include <Ayria.hpp>
#include "Communication.hpp"

// Primarilly for user interaction, but can be called from plugins as well.
namespace Communication::Console
{
    // UTF8 escaped ASCII strings are passed to argv for compatibility with C-plugins.
    // using Functioncallback_t = void(__cdecl *)(int argc, const char **argv);
    // using Logline_t = std::pair<std::u8string, uint32_t>;

    static Debugmutex_t Writelock{};
    static constexpr size_t Loglimit{ 128 };
    static Ringbuffer_t<Logline_t, Loglimit> Consolelog{};
    static Hashmap<std::u8string, Hashset<Functioncallback_t>> Commands{};

    // Useful for checking for new messages.
    std::atomic<uint32_t> LastmessageID{};

    // Threadsafe injection and fetching from the global log.
    void addMessage(Logline_t &&Message)
    {
        const auto &[String, Color] = Message;
        const auto Lines = Stringsplit(String, '\n');

        // If there's no color specified, try to deduce it.
        if (Color == 0)
        {
            std::scoped_lock Threadguard(Writelock);
            for (const auto &Line : Lines)
            {
                const auto Newcolor = [&]() -> uint32_t
                {
                    // Ayria common prefixes.
                    if (Line.contains(u8"[E]")) return 0x00BE282A;
                    if (Line.contains(u8"[W]")) return 0x002AC0BE;
                    if (Line.contains(u8"[I]")) return 0x00BD8F21;
                    if (Line.contains(u8"[D]")) return 0x003E967F;
                    if (Line.contains(u8"[>]")) return 0x007F963E;

                    // Hail Mary..
                    if (Line.contains(u8"rror")) return 0x00BE282A;
                    if (Line.contains(u8"arning")) return 0x002AC0BE;

                    // Default.
                    return 0x00315571;
                }();

                Consolelog.emplace_back(Line, Newcolor);
            }
        }
        else
        {
            std::scoped_lock Threadguard(Writelock);
            for (const auto &Line : Lines)
            {
                Consolelog.emplace_back(Line, Color);
            }
        }

        // Just need a different ID.
        ++LastmessageID;
    }
    std::vector<Logline_t> getMessages(size_t Maxcount, std::u8string_view Filter)
    {
        auto Clamped = std::min(Maxcount, Loglimit);
        std::vector<Logline_t> Result;
        Result.reserve(Clamped);

        {
            std::scoped_lock Threadguard(Writelock);
            std::ranges::copy_if(Consolelog | std::views::reverse, std::back_inserter(Result),
                                 [&](const auto &Tuple) -> bool
                                 {
                                     if (0 == Clamped) return false;

                                     // An empty filter is always true.
                                     if (Tuple.first.contains(Filter))
                                     {
                                         Clamped--;
                                         return true;
                                     }

                                     return false;
                                 });
        }

        // Reverse the vector to simplify other code.
        std::ranges::reverse(Result);
        return Result;
    }

    // Case-insensitive search for the string.
    static Hashset<Functioncallback_t> *Findcommand(std::u8string_view Command)
    {
        for (const auto &[Name, List] : Commands)
        {
            // Early exit.
            if (Command.size() != Name.size()) [[likely]]
                continue;

            // Lazily evaluated.
            const auto Input = Command | std::views::transform([](char8_t c) {return std::toupper(c); });
            const auto This = Name | std::views::transform([](char8_t c) {return std::toupper(c); });

            if (std::ranges::equal(Input, This))
            {
                return &Commands[Name];
            }
        }

        return nullptr;
    }

    // Manage and execute the commandline, with optional logging.
    void execCommand(std::u8string_view Commandline, bool Log)
    {
        const auto Tokens = Tokenizestring(Commandline);
        const auto Size = Tokens.size();

        // Why would you do this? =(
        if (Size == 0) [[unlikely]] return;

        // Format as a C-array with the last pointer being null.
        const auto Arguments = std::make_unique<char *[]>(Size + 1);
        for (size_t i = 0; i < Size; ++i)
        {
            Arguments[i] = new char[Tokens[i].size() + 1]();
            std::memcpy(Arguments[i], Tokens[i].data(), Tokens[i].size());
        }

        // The first argument should always be the command.
        const auto Handlers = Findcommand(Tokens[0]);
        if (!Handlers)
        {
            Errorprint(va("No command named: %*s", Tokens[0].size(), Tokens[0].data()));
            for (size_t i = 0; i < Size; ++i) delete[] Arguments[i];
            return;
        }

        // Notify the user about what was executed.
        if (Log) [[likely]]
            addMessage({ u8"> "s + std::u8string(Commandline.data(), Commandline.size()), 0x00D6B749 });

        // Evaluate the command.
        for (const auto &Callback : *Handlers)
            Callback(int(Size - 1), const_cast<const char **>(&Arguments[1]));

        // Basic cleanup.
        for (size_t i = 0; i < Size; ++i)
            delete[] Arguments[i];
    }
    void addCommand(std::u8string_view Name, Functioncallback_t Callback)
    {
        Commands[{Name.data(), Name.size()}].insert(Callback);
    }

    // Add some generic commands.
    static void Defaultcommands()
    {
        static constexpr auto Quit = [](int, const char **)
        {
            Backend::Backgroundtasks::Terminate();
        };
        addCommand(u8"Quit", Quit);
        addCommand(u8"Exit", Quit);

        static constexpr auto List = [](int, const char **)
        {
            std::u8string Output; Output.reserve(20 * Commands.size());
            for (const auto &[Index, Names] : Enumerate(Commands | std::views::keys, 1))
            {
                Output += u8"\t";
                Output += Names;
                if (Index % 4 == 0) Output += u8'\n';
            }

            addMessage(u8"Available commands:", 0x00BD8F21);
            addMessage(Output, 0x715531);
        };
        addCommand(u8"List", List);
        addCommand(u8"Help", List);
    }

    // Access from the plugins.
    namespace Export
    {
        extern "C" EXPORT_ATTR void __cdecl addConsolecommand(const char *Name, void(__cdecl *Callback)(int Argc, const char **Argv))
        {
            if (!Name || !Callback) [[unlikely]] return;
            addCommand(Encoding::toUTF8(Name), Callback);
        }
        extern "C" EXPORT_ATTR void __cdecl addConsolemessage(const char *String, uint32_t ARGBColor)
        {
            if (!String) [[unlikely]] return;
            addMessage(Encoding::toUTF8(String), ARGBColor);
        }
        extern "C" EXPORT_ATTR void __cdecl execCommand(const char *Commandline)
        {
            if (!Commandline) [[unlikely]] return;
            Console::execCommand(Encoding::toUTF8(Commandline));
        }
    }
    namespace Endpoint
    {
        static std::string __cdecl addConsolemessage(JSON::Value_t &&Request)
        {
            const auto Color = std::max(Request.value<uint32_t>("Color"), Request.value<uint32_t>("Colour"));
            const auto Message = Request.value<std::u8string>("Message");

            addMessage({ Message, Color });
            return "{}";
        }
        static std::string __cdecl execCommand(JSON::Value_t &&Request)
        {
            const auto Commandline = Request.value<std::u8string>("Commandline");
            const auto Shouldlog = Request.value<bool>("Log");

            Console::execCommand(Commandline, Shouldlog);
            return "{}";
        }
    }

    // Set up the endpoints.
    const struct Startup_t
    {
        Startup_t()
        {
            Backend::Backgroundtasks::addStartuptask([]
            {
                JSONAPI::addEndpoint("Console::addMessage", Endpoint::addConsolemessage);
                JSONAPI::addEndpoint("Console::execCommand", Endpoint::execCommand);

                Defaultcommands();
            });
        }
    } Startup{};
}
