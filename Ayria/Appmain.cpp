/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-09-12
    License: MIT
*/

#include <Ayria.hpp>

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
    // On startup.
    if (nReason == DLL_PROCESS_ATTACH)
    {
        // Although it should already be touched by the ctors, ensure it's propagated and prioritized.
        //_mm_prefetch(reinterpret_cast<const char *>(&Global), _MM_HINT_T0);

        // Ensure that Ayrias default directories exist.
        InitializeFS();

        // Clear the previouslog and set up a new one.
        Logging::Initialize();

        // Catch any unwanted exceptions.
        SetUnhandledExceptionFilter(onUnhandledexception);

        // Opt out of further notifications.
        DisableThreadLibraryCalls(hDllHandle);

        // Initialize the background tasks.
        Backend::Taskrunner::runStartup();

        // If injected, we can't hook. So just load all plugins directly.
        if (lpvReserved == NULL)
        {
            //Plugins::Initialize();
        }

        // Else we try to hook the TLS or EP.
        else
        {
            // We prefer TLS-hooks over EP.
            //if (Plugins::InstallTLSHook()) return TRUE;

            // Fallback to EP hooking.
            //if (!Plugins::InstallEPHook())
            {
                MessageBoxA(NULL, "Could not install a hook in the host application", "Fatal error", NULL);
                return FALSE;
            }
        }
    }

    return TRUE;
}
