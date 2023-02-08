/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-12
    License: MIT
*/

#include <Ayria.hpp>
#include <stacktrace>
#include "dbghelp.h"

// 512-bit aligned storage.
Globalstate_t Global{};

// Some applications do not handle exceptions well.
static LONG __stdcall onUnhandledexception(PEXCEPTION_POINTERS Context)
{
    // OpenSSLs RAND_poll() causes Windows to throw if RPC services are down.
    if (Context->ExceptionRecord->ExceptionCode == RPC_S_SERVER_UNAVAILABLE)
    {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // DirectSound does not like it if the Audio services are down.
    if (Context->ExceptionRecord->ExceptionCode == RPC_S_UNKNOWN_IF)
    {
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Semi-documented way for naming Windows threads.
    if (Context->ExceptionRecord->ExceptionCode == 0x406D1388)
    {
        if (Context->ExceptionRecord->ExceptionInformation[0] == 0x1000)
        {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }

    // Log a stacktrace.
    if constexpr (Build::isDebug)
    {
        const auto String = std::to_string(std::stacktrace::current());
        Errorprint(va("================== Unhandled exception ==================\n%s\n", String.c_str()));
    }

    // Make a nice little dump.
    if (const auto Module = LoadLibraryA("dbghelp.dll"))
    {
        using MINIDUMPWRITEDUMP = BOOL(__stdcall *)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType, PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

        const auto Proc = (MINIDUMPWRITEDUMP)GetProcAddress(Module, "MiniDumpWriteDump");

        if (const auto File = CreateFileA(MODULENAME ".dmp", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL))
        {
            auto ExInfo = _MINIDUMP_EXCEPTION_INFORMATION{ GetCurrentThreadId(), Context };

            Proc(GetCurrentProcess(), GetCurrentProcessId(), File, MiniDumpWithFullMemory, &ExInfo, NULL, NULL);

            CloseHandle(File);
        }

        FreeLibrary(Module);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Probably crash.
    return EXCEPTION_CONTINUE_SEARCH;
}

// Ensure that the default directories exists.
static void InitializeFS()
{
    std::filesystem::create_directories("./Ayria/Logs");
    std::filesystem::create_directories("./Ayria/Storage");
    std::filesystem::create_directories("./Ayria/Plugins");
}

// Entrypoint when loaded as a shared library.
BOOLEAN __stdcall DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID lpvReserved)
{
    // On startup, either as a normal DLL or as verifier.
    if (nReason == DLL_PROCESS_ATTACH || nReason == 4)
    {
        // Although it should already be touched by the ctors, ensure it's propagated into L2.
        _mm_prefetch(reinterpret_cast<const char *>(&Global), _MM_HINT_T1);

        // Ensure that Ayrias default directories exist.
        InitializeFS();

        // Clear the previouslog and set up a new one.
        Logging::Initialize();

        // Catch any unwanted exceptions.
        SetUnhandledExceptionFilter(onUnhandledexception);

        // Opt out of further notifications.
        DisableThreadLibraryCalls(hDllHandle);

        // Load the configuration from disk (if available).
        Backend::Config::Load();

        // Initialize the background tasks.
        Backend::Backgroundtasks::Initialize();

        // If injected, we can't hook. So just load all plugins directly.
        if (lpvReserved == NULL)
        {
            Backend::Plugins::Initialize();
        }

        // Else we try to hook the TLS or EP.
        else
        {
            // We prefer TLS-hooks over EP.
            if (Backend::Plugins::InstallTLSHook()) return TRUE;

            // Fallback to EP hooking.
            if (!Backend::Plugins::InstallEPHook())
            {
                MessageBoxA(NULL, "Could not install a hook in the host application", "Fatal error", NULL);
                return FALSE;
            }
        }
    }

    return TRUE;
}
