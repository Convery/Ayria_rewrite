/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-21
    License: MIT

    For how to get the ranges on iOS: https://stackoverflow.com/questions/25286221/how-to-find-text-segment-range-in-ios
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Hacking
{
    using Memoryrange_t = std::pair<std::uintptr_t, std::uintptr_t>;

    // Fetch some default ranges, assumes a normal segment layout.
    inline Memoryrange_t getCoderange(bool Cache = true)
    {
        static Memoryrange_t Range{};

        // Use the cached version of the range.
        if (Range.first && Range.second && Cache)
            return Range;

        // Use the platform provided utilities.
        #if !defined (_WIN32)
        Range.first = std::uintptr_t(_ftext);
        Range.second = std::uintptr_t(_etext);

        #else
        HMODULE Module = GetModuleHandleA(nullptr);

        // The process have AppGuard or something similar going on.
        if (!Module) Errorprint("Can't open our own module, some fuckery going on.");

        const auto DOSHeader = (PIMAGE_DOS_HEADER)Module;
        const auto NTHeader = (PIMAGE_NT_HEADERS)((std::uintptr_t)Module + DOSHeader->e_lfanew);

        Range.first = std::uintptr_t(Module) + NTHeader->OptionalHeader.BaseOfCode;
        Range.second = Range.first + NTHeader->OptionalHeader.SizeOfCode;

        CloseHandle(Module);
        #endif

        return Range;
    }
    inline Memoryrange_t getDatarange(bool Cache = true)
    {
        static Memoryrange_t Range{};

        // Use the cached version of the range.
        if (Range.first && Range.second && Cache)
            return Range;

        // Use the platform provided utilities.
        #if !defined (_WIN32)
        Range.first = std::uintptr_t(_fdata);
        Range.second = std::uintptr_t(edata);

        #else
        HMODULE Module = GetModuleHandleA(nullptr);

        // The process have AppGuard or something similar going on.
        if (!Module) Errorprint("Can't open our own module, some fuckery going on.");

        const auto DOSHeader = (PIMAGE_DOS_HEADER)Module;
        const auto NTHeader = (PIMAGE_NT_HEADERS)((std::uintptr_t)Module + DOSHeader->e_lfanew);

        // BaseOfData is only defined for PE32, we assume that for PE32+ it follows the text.
        #if !defined (_WIN64)
        Range.first = std::uintptr_t(Module) + NTHeader->OptionalHeader.BaseOfData;
        #else
        Range.first = std::uintptr_t(Module) + NTHeader->OptionalHeader.BaseOfCode + NTHeader->OptionalHeader.SizeOfCode;
        #endif

        Range.second = Range.first + NTHeader->OptionalHeader.SizeOfInitializedData;

        CloseHandle(Module);
        #endif

        return Range;
    }
    inline Memoryrange_t getVirtualrange()
    {
        Memoryrange_t Range{ getDatarange().second, 0 };

        #if defined(_WIN32)
        MEMORY_BASIC_INFORMATION Pageinformation{};
        auto Currentpage = Range.first;

        while (VirtualQueryEx(GetCurrentProcess(), (LPCVOID)(Currentpage + 1), &Pageinformation, sizeof(MEMORY_BASIC_INFORMATION)))
        {
            if (Pageinformation.State != MEM_COMMIT) break;
            Currentpage = (size_t)Pageinformation.BaseAddress + Pageinformation.RegionSize;
        }

        #else

        std::FILE *Filehandle = std::fopen("/proc/self/maps", "r");
        if (Filehandle)
        {
            char Buffer[1024]{}, Permissions[5]{}, Device[6]{}, Mapname[256]{};
            unsigned long Start, End, Node, Foo;

            while (std::fgets(Buffer, 1024, Filehandle))
            {
                std::sscanf(Buffer, "%lx-%lx %4s %lx %5s %lu %s", &Start, &End, Permissions, &Foo, Device, &Node, Mapname);

                if (Start >= Range.first)
                {
                    Range.second = End;
                    break;
                }
            }

            std::fclose(Filehandle);
        }
        #endif

        Range.second = Currentpage;
        return Range;
    }

    // NOTE(tcn): We do not handle SEC_NO_CHANGE protected ranges.
    inline bool Protectrange(std::uintptr_t Address, size_t Size, unsigned long Protection)
    {
        #if defined(_WIN32)
        unsigned long Temp;
        return VirtualProtect((void *)Address, Size, Protection, &Temp);

        #else
        const auto Pagesize = sysconf(_SC_PAGE_SIZE);
        Address -= Address % Pagesize;
        return 0 == mprotect((void *)Address, Size + ((Size % Pagesize) ? (Pagesize - (Size % Pagesize)) : 0), Protection);

        #endif
    }
    inline unsigned long Unprotectrange(std::uintptr_t Address, size_t Size, bool Executable = false)
    {
        unsigned long Originalprotection{};

        #if defined(_WIN32)
        if (Executable)
            VirtualProtect((void *)Address, Size, PAGE_EXECUTE_READWRITE, &Originalprotection);
        else
            VirtualProtect((void *)Address, Size, PAGE_READWRITE, &Originalprotection);

        #else

        // Get the old protection of the range, we assume it's continuous.
        std::FILE *Filehandle = std::fopen("/proc/self/maps", "r");
        if (Filehandle)
        {
            char Buffer[1024]{}, Permissions[5]{}, Device[6]{}, Mapname[256]{};
            unsigned long Start, End, Node, Foo;

            while (std::fgets(Buffer, 1024, Filehandle))
            {
                std::sscanf(Buffer, "%lx-%lx %4s %lx %5s %lu %s", &Start, &End, Permissions, &Foo, Device, &Node, Mapname);

                if (Start <= Address && End >= Address)
                {
                    Originalprotection = 0;

                    if (Permissions[0] == 'r') Originalprotection |= PROT_READ;
                    if (Permissions[1] == 'w') Originalprotection |= PROT_WRITE;
                    if (Permissions[2] == 'x') Originalprotection |= PROT_EXEC;

                    break;
                }
            }

            std::fclose(Filehandle);
        }

        // Write the new protection.
        const auto Pagesize = sysconf(_SC_PAGE_SIZE);
        Address -= Address % Pagesize;
        mprotect((void *)Address, Size + ((Size % Pagesize) ? (Pagesize - (Size % Pagesize)) : 0), (PROT_READ | PROT_WRITE | (Executable ? PROT_EXEC : 0)));

        #endif

        return Originalprotection;
    }

    // RTTI version for unprotecting a range.
    struct RTTI_Memprotect final
    {
        const std::uintptr_t lAddress;
        unsigned long lProtection;
        const size_t lSize;

        explicit RTTI_Memprotect(const std::uintptr_t Address, const size_t Size) : lAddress(Address), lSize(Size)
        {
            lProtection = Unprotectrange(lAddress, lSize);
        }
        ~RTTI_Memprotect()
        {
            Protectrange(lAddress, lSize, lProtection);
        }
    };
    [[nodiscard]] inline RTTI_Memprotect Make_writeable(std::uintptr_t Address, const size_t Size)
    {
        return RTTI_Memprotect(Address, Size);
    }
    [[nodiscard]] inline RTTI_Memprotect Make_writeable(const void *Address, const size_t Size)
    {
        return RTTI_Memprotect(reinterpret_cast<std::uintptr_t>(Address), Size);
    }
}
