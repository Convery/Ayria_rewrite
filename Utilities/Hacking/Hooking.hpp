/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-21
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include <Utilities/Hacking/Memory.hpp>

namespace Hacking
{
    // Minimum size for a x64 non-destructive absolute jump.
    constexpr size_t Jumpsize = Build::is64bit ? 14 : 5;

    // A simple hook that jumps to the replacement, which return execution.
    inline void Stomphook(std::uintptr_t Target, std::uintptr_t Replacement)
    {
        const auto Lock = Make_writeable(Target, Jumpsize);

        if constexpr (Build::is64bit)
        {
            // JMP [RIP] | FF 25 00 00 00 00 Target
            *(uint8_t *)(Target + 0) = 0xFF;
            *(uint8_t *)(Target + 1) = 0x25;
            *(uint32_t *)(Target + 2) = 0x0;
            *(uint64_t *)(Target + 6) = Replacement;
        }
        else
        {
            // JMP rel32 | E9 Target
            *(uint8_t *)(Target + 0) = 0xE9;
            *(uint32_t *)(Target + 1) = Replacement - Target - 5;
        }
    }

    // Trampoline hook to allow for continuing execution.
    inline std::optional<std::pair<std::uintptr_t, size_t>> Callhook(std::uintptr_t Target, std::uintptr_t Replacement)
    {
        // Maximum executable length of a x86 instruction is 16 bytes.
        const auto Lock = Make_writeable(Target, Jumpsize + 15);

        // Disassemble at the target to get how much code to copy.
        const auto Oldlength = getInstructionboundry(Target, Jumpsize);
        if (!Oldlength) return std::nullopt;

        // Allocate a funky little trampoline, the user can deallocate or leak at their discression.
        const auto Trampoline = (std::uintptr_t)VirtualAlloc(NULL, *Oldlength + Jumpsize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!Trampoline) return std::nullopt;

        // Save the original code.
        std::memcpy((void *)Target, (void *)Trampoline, *Oldlength);

        // Set up the trampoline.
        if constexpr (Build::is64bit)
        {
            // JMP [RIP] | FF 25 00 00 00 00 Target
            *(uint8_t *)(Trampoline + *Oldlength + 0) = 0xFF;
            *(uint8_t *)(Trampoline + *Oldlength + 1) = 0x25;
            *(uint32_t *)(Trampoline + *Oldlength + 2) = 0x0;
            *(uint64_t *)(Trampoline + *Oldlength + 6) = Target + *Oldlength;
        }
        else
        {
            // JMP rel32 | E9 Target
            *(uint8_t *)(Trampoline + *Oldlength + 0) = 0xE9;
            *(size_t *)(Trampoline + *Oldlength + 1) = (Target + *Oldlength) - (Trampoline + *Oldlength + 5);
        }

        // Stomphook to the replacement.
        if constexpr (Build::is64bit)
        {
            // JMP [RIP] | FF 25 00 00 00 00 Target
            *(uint8_t *)(Target + 0) = 0xFF;
            *(uint8_t *)(Target + 1) = 0x25;
            *(uint32_t *)(Target + 2) = 0x0;
            *(uint64_t *)(Target + 6) = Replacement;
        }
        else
        {
            // JMP rel32 | E9 Target
            *(uint8_t *)(Target + 0) = 0xE9;
            *(size_t *)(Target + 1) = Replacement - Target - 5;
        }

        return { { Trampoline, *Oldlength } };
    }

    // For when a call isn't what we want.
    [[noreturn]] INLINE_ATTR void Jump(std::uintptr_t Target)
    {
        #if defined(_WIN32)
        CONTEXT Context{ .ContextFlags = CONTEXT_CONTROL };
        GetThreadContext(GetCurrentThread(), &Context);

        #if defined(_M_X64) || defined(__x86_64__)
        Context.Rip = Target;
        #else
        Context.Eip = Target;
        #endif

        SetThreadContext(GetCurrentThread(), &Context);
        std::unreachable();
        #else

        ucontext_t Context;
        getcontext(&Context);

        #if defined(_M_X64) || defined(__x86_64__)
        Context.uc_mcontext.gregs[REG_RIP] = Target;
        #else
        Context.uc_mcontext.gregs[REG_EIP] = Target;
        #endif

        setcontext(&Context);
        std::unreachable();
        #endif
    }

    // For when we want to change our caller.
    INLINE_ATTR void setReturn(std::uintptr_t Target)
    {
        #if defined (_MSC_VER)
        *(size_t *)_AddressOfReturnAddress() = Target;

        #elif __GNUC__ || __has_builtin(__builtin_frame_address)
        *(size_t *)(__builtin_frame_address(0) + sizeof(size_t)) = Target;

        #endif
    }

    // Helpers for when the user prefers pointers.
    template <typename T, typename U> auto Stomphook(T Target, U Replacement) { return Stomphook(std::uintptr_t(Target), std::uintptr_t(Replacement)); }
    template <typename T, typename U> auto Callhook(T Target, U Replacement) { return Callhook(std::uintptr_t(Target), std::uintptr_t(Replacement)); }

    // For PE-IAT hooking, find the pointer that points to the wanted address.
    #if defined(_WIN32)
    inline std::uintptr_t getIATpointer(HMODULE Targetmodule, std::uintptr_t Targetaddress)
    {
        // Traverse the PE header.
        const auto *DOSHeader = PIMAGE_DOS_HEADER(Targetmodule);
        const auto *NTHeader = PIMAGE_NT_HEADERS(DWORD_PTR(Targetmodule) + DOSHeader->e_lfanew);
        const auto Directory = NTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const auto *IAT = PIMAGE_IMPORT_DESCRIPTOR(DWORD_PTR(Targetmodule) + Directory.VirtualAddress);

        // Should never happen outside of embedded systems.
        if (Directory.Size == NULL) [[unlikely]] return NULL;

        while (IAT->Name && IAT->OriginalFirstThunk)
        {
            const auto *Thunkdata = PIMAGE_THUNK_DATA(DWORD_PTR(Targetmodule) + IAT->OriginalFirstThunk);
            const auto *Thunk = PIMAGE_THUNK_DATA(DWORD_PTR(Targetmodule) + IAT->FirstThunk);
            while (Thunkdata->u1.AddressOfData)
            {
                if (Thunk->u1.Function && *(std::uintptr_t *)Thunk->u1.Function == Targetaddress)
                    return Thunk->u1.Function;

                // Advance to the next thunk.
                ++Thunk; ++Thunkdata;
            }

            // Advance to the next descriptor.
            ++IAT;
        }

        return NULL;
    }
    inline std::uintptr_t getIATpointer(HMODULE Targetmodule, const std::string &Importmodulename, size_t Exportordinal)
    {
        const auto Modulehandle = GetModuleHandleA(Importmodulename.c_str());
        if (!Modulehandle) return NULL;

        const auto Target = GetProcAddress(Modulehandle, LPCSTR(Exportordinal));
        if (!Target) return NULL;

        return getIATpointer(Targetmodule, std::uintptr_t(Target));
    }
    inline std::uintptr_t getIATpointer(HMODULE Targetmodule, const std::string &Importmodulename, const std::string &Exportname)
    {
        const auto Modulehandle = GetModuleHandleA(Importmodulename.c_str());
        if (!Modulehandle) return NULL;

        const auto Target = GetProcAddress(Modulehandle, Exportname.c_str());
        if (!Target) return NULL;

        return getIATpointer(Targetmodule, std::uintptr_t(Target));
    }
    #endif

    // Should really be an intrinsic..
    INLINE_ATTR void __stdcall PUSHA();
    INLINE_ATTR void __stdcall POPA();
}
