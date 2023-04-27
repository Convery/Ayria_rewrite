/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-02-04
    License: MIT

    We need to wait for the static dlls to be loaded before plugins.
    Our options are replacing a TLS callback or hooking the EP.
*/

#include <Ayria.hpp>

namespace Backend::Plugins
{
    // Helper to write a pointer.
    #define Writeptr(where, what) {                                             \
    const auto Lock = Hacking::Make_writeable((where), sizeof(std::uintptr_t)); \
    *(std::uintptr_t *)(where) = (std::uintptr_t)(what); }

    static Inlinedvector<std::uintptr_t, 4> OriginalTLS{};
    static Hashset<HMODULE> Pluginhandles{};
    static std::atomic_flag Initialized{};
    static std::uintptr_t EPTrampoline{};
    static size_t EPSize{};

    // Helpers for reading the header.
    static std::uintptr_t getTLSEntry()
    {
        #if !defined (_WIN32)
        return NULL;
        #else
        // Module(NULL) gets the host application.
        const HMODULE Modulehandle = GetModuleHandleA(NULL);
        if (!Modulehandle) return NULL;

        // Traverse the PE header.
        const auto *DOSHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(Modulehandle);
        const auto *NTHeader = PIMAGE_NT_HEADERS(std::uintptr_t(Modulehandle) + DOSHeader->e_lfanew);
        const auto Directory = NTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];

        if (Directory.Size == 0) return NULL;
        return std::uintptr_t(Modulehandle) + Directory.VirtualAddress;
        #endif
    }
    static std::uintptr_t getPEEntrypoint()
    {
        #if !defined (_WIN32)
        return NULL;
        #else
        // Module(NULL) gets the host application.
        const HMODULE Modulehandle = GetModuleHandleA(NULL);
        if (!Modulehandle) return NULL;

        // Traverse the PE header.
        const auto *DOSHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(Modulehandle);
        const auto *NTHeader = PIMAGE_NT_HEADERS(std::uintptr_t(Modulehandle) + DOSHeader->e_lfanew);
        return reinterpret_cast<std::uintptr_t>(Modulehandle) + NTHeader->OptionalHeader.AddressOfEntryPoint;
        #endif
    }
    static std::uintptr_t getELFEntrypoint()
    {
        #if defined (_MSC_VER)
        return NULL;
        #else
        // dlopen(NULL) gets the host application.
        void *Modulehandle = dlopen(NULL, RTLD_LAZY);
        if(!Modulehandle) return NULL;

        return std::uintptr_t(dlsym(Modulehandle, "__libc_start_main"));
        #endif
    }

    // Initialize the plugins.
    static void Notifystartup()
    {
        for (const auto Handle : Pluginhandles)
        {
            if (const auto Func = GetProcAddress(Handle, "onStartup"); Func)
            {
                (reinterpret_cast<void(__cdecl *)(bool)>(Func))(Global.State.Pluginflag);
            }
        }
    }
    static void Notifyinitialized()
    {
        if (Initialized.test_and_set()) return;

        for (const auto Handle : Pluginhandles)
        {
            if (const auto Func = GetProcAddress(Handle, "onInitialized"); Func)
            {
                (reinterpret_cast<void(__cdecl *)(bool)>(Func))(Global.State.Pluginflag);
            }
        }
    }

    // Broadcast a message to all plugins.
    void Broadcast(uint32_t MessageID, const std::string &JSONString)
    {
        if constexpr (Build::isDebug)
        {
            const auto Checksum = Hash::WW32(JSONString);

            for (const auto Handle : Pluginhandles) if (const auto Func = GetProcAddress(Handle, "onMessage"); Func)
            {
                (reinterpret_cast<void(__cdecl *)(unsigned int, const char *, unsigned int)>(Func))(MessageID, JSONString.c_str(), (unsigned int)JSONString.size());

                // Verify that the plugin respected const-ness.
                assert(Checksum == Hash::WW32(JSONString));
            }
        }
        else
        {
            for (const auto Handle : Pluginhandles) if (const auto Func = GetProcAddress(Handle, "onMessage"); Func)
            {
                (reinterpret_cast<void(__cdecl *)(unsigned int, const char *, unsigned int)>(Func))(MessageID, JSONString.c_str(), (unsigned int)JSONString.size());
            }
        }
    }

    // Simply load all plugins from disk.
    void Initialize()
    {
        // Load all plugins from disk.
        for (const auto Items = FS::Findfiles(L"./Ayria/Plugins", Build::is64bit ? L"64" : L"32"); const auto &Item : Items)
        {
            if (const auto Module = LoadLibraryW((L"./Ayria/Plugins/"s + Item).c_str()))
            {
                Pluginhandles.insert(Module);
            }
        }

        // Notify about startup.
        Notifystartup();

        // Ensure that a "onInitialized" is sent 'soon'.
        [[maybe_unused]] static auto doOnce{ []() -> bool
        {
            std::thread([]()
            {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                Notifyinitialized();
            }).detach();

            return {};
        }() };
    }

    // Callbacks from the hooks, __libc_start_main for ELF.
    int ELFCallback(int (*main) (int, char **, char **), int argc, char **ubp_av, void (*init) (), void (*fini) (), void (*rtld_fini) (), void *stack_end)
    {
        if (const auto EP = getELFEntrypoint())
        {
            // Restore the code in-case the app does integrity checking.
            const auto RAII = Hacking::Make_writeable(EP, EPSize);
            std::memcpy((void *)EP, (void *)EPTrampoline, EPSize);
        }

        // Load all plugins.
        Initialize();

        // Resume via the trampoline.
        return (reinterpret_cast<decltype(ELFCallback) *>(EPTrampoline))(main, argc, ubp_av, init, fini, rtld_fini, stack_end);
    }
    void __stdcall TLSCallback(PVOID a, DWORD b, PVOID c)
    {
        if (const auto Directory = (PIMAGE_TLS_DIRECTORY)getTLSEntry())
        {
            auto Callbacks = (std::uintptr_t *)Directory->AddressOfCallBacks;
            HMODULE Valid;

            // Disable TLS while loading plugins.
            Writeptr(&Callbacks[0], nullptr);
            Initialize();

            // Restore the TLS directory.
            for (const auto &Address : OriginalTLS)
            {
                Writeptr(Callbacks, Address);
                Callbacks++;
            }

            // Call the first real callback if we had one.
            Callbacks = reinterpret_cast<size_t *>(Directory->AddressOfCallBacks);
            if (*Callbacks && GetModuleHandleExA(6, LPCSTR(*Callbacks), &Valid))
                reinterpret_cast<decltype(TLSCallback) *>(*Callbacks)(a, b, c);
        }
    }
    void PECallback()
    {
        // Read the PE header.
        if (const auto EP = getPEEntrypoint())
        {
            // Restore the code in-case the app does integrity checking.
            const auto RAII = Hacking::Make_writeable(EP, EPSize);
            std::memcpy((void *)EP, (void *)EPTrampoline, EPSize);
        }

        // Load all plugins.
        Initialize();

        // Resume via the trampoline.
        (reinterpret_cast<decltype(PECallback) *>(EPTrampoline))();
    }

    // Different types of hooking.
    bool InstallTLSHook()
    {
        if (const auto Directory = reinterpret_cast<PIMAGE_TLS_DIRECTORY>(getTLSEntry()))
        {
            // Save any and all existing callbacks.
            auto Callbacks = (std::uintptr_t *)Directory->AddressOfCallBacks;
            while (*Callbacks) OriginalTLS.push_back(*Callbacks++);

            // Replace with ours.
            Callbacks = (std::uintptr_t *)Directory->AddressOfCallBacks;
            Writeptr(&Callbacks[0], TLSCallback);
            Writeptr(&Callbacks[1], NULL);

            return true;
        }

        return false;
    }
    bool InstallEPHook()
    {
        // Most likely we are looking for a PE(+) file.
        if (const auto EP = getPEEntrypoint())
        {
            const auto Optional = Hacking::Callhook(EP, PECallback);
            if (!Optional) return false;

            std::tie(EPTrampoline, EPSize) = *Optional;
            return true;
        }

        // But maybe
        if (const auto EP = getELFEntrypoint())
        {
            const auto Optional = Hacking::Callhook(EP, ELFCallback);
            if (!Optional) return false;

            std::tie(EPTrampoline, EPSize) = *Optional;
            return true;
        }


        return false;
    }

    // Should be called from platformwrapper or similar plugins once application is done loading.
    extern "C" EXPORT_ATTR void __cdecl onInitialized()
    {
        Notifyinitialized();
    }
}
