/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2023-01-21
    License: MIT

    Relies on std::memchr being optimized by the compiler.
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Hacking
{
    // Generally retrieved from Memory.hpp
    using Memoryrange_t = std::pair<std::uintptr_t, std::uintptr_t>;

    namespace Patternscan
    {
        using Patternmask_t = std::basic_string<uint8_t>;

        // Find the address of a single pattern in the range.
        inline std::uintptr_t Findpattern(const Memoryrange_t &Range, const Patternmask_t &Pattern, const Patternmask_t &Mask)
        {
            assert(Pattern.size() == Mask.size());  assert(Pattern.size() != 0);
            assert(Range.second != 0); assert(Range.first != 0);
            assert(Mask[0] != 0); // The first byte can't be a wildcard.

            size_t Count = Range.second - Range.first - Pattern.size();
            auto Base = (const uint8_t *)Range.first;
            const uint8_t Firstbyte = Pattern[0];

            // Inline compare.
            const auto Compare = [&](const uint8_t *Address) -> bool
            {
                const size_t Patternlength = Pattern.size();
                for (size_t i = 1; i < Patternlength; ++i)
                {
                    if (Mask[i] && Address[i] != Pattern[i])
                        return false;
                }
                return true;
            };

            while (true)
            {
                // Use memchr as the compiler can use an optimized scan rather than a for loop.
                const auto Piviot = (const uint8_t *)std::memchr(Base, Firstbyte, Count);

                // No interesting byte in the range.
                if (!Piviot) [[unlikely]]
                    return 0;

                // Rare case that something is found.
                if (Compare(Piviot)) [[unlikely]]
                    return std::uintptr_t(Piviot);

                Base = Piviot + 1;
                Count = Range.second - std::uintptr_t(Base) - Pattern.size();
            }
        }

        // Scan until the end of the range and return all results.
        inline std::vector<std::uintptr_t> Findpatterns(const Memoryrange_t &Range, const Patternmask_t &Pattern, const Patternmask_t &Mask)
        {
            std::vector<std::uintptr_t> Results;
            Memoryrange_t Localrange = Range;
            size_t Lastresult = 0;

            while (true)
            {
                Lastresult = Findpattern(Localrange, Pattern, Mask);

                if (Lastresult == 0)
                    break;

                Localrange.first = Lastresult + 1;
                Results.push_back(Lastresult);
            }

            return Results;
        }

        // Create a pattern or mask from a readable string.
        inline std::pair<Patternmask_t, Patternmask_t> from_string(const std::string_view Readable)
        {
            constexpr uint8_t Hex[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                0, 0, 0, 0, 0, 0, 0, 10, 11, 12, 13, 14, 15, 0,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 10, 11, 12, 13, 14, 15 };

            Patternmask_t Pattern{}, Mask{};
            uint32_t Count{ 0 };

            // Preallocate for worst case.
            Pattern.reserve(Readable.size() >> 1);
            Mask.reserve(Readable.size() >> 1);

            for (const auto &Item : Readable)
            {
                if (Item == ' ') { Count = 0; continue; }
                if (Item == '?') { Pattern.push_back('\x00'); Mask.push_back('\x00'); }
                else
                {
                    if (++Count & 1) Mask.push_back('\x01');

                    if (Count & 1) Pattern.push_back(Hex[Item - 0x30]);
                    else Pattern.back() = (Pattern.back() << 4) | Hex[Item - 0x30];
                }
            }

            return { Pattern, Mask };
        }
    }

    // IDA style pattern. "00 01 ? ? 04" == "0 1 ?? 4"
    inline std::vector<std::uintptr_t> Findpatterns(const Memoryrange_t &Range, const std::string &IDAPattern)
    {
        const auto [Pattern, Mask] = Patternscan::from_string(IDAPattern);
        return Patternscan::Findpatterns(Range, Pattern, Mask);
    }
    inline std::uintptr_t Findpattern(const Memoryrange_t &Range, const std::string &IDAPattern)
    {
        const auto [Pattern, Mask] = Patternscan::from_string(IDAPattern);
        return Patternscan::Findpattern(Range, Pattern, Mask);
    }

    // Helper when searching for strings in memory.
    inline std::vector<std::uintptr_t> Findstrings(const Memoryrange_t &Range, const std::string &Literal)
    {
        const auto Patternmask = cmp::getBytes(Literal);
        return Patternscan::Findpatterns(Range, Patternmask, Patternmask);
    }
    inline std::uintptr_t Findstring(const Memoryrange_t &Range, const std::string &Literal)
    {
        const auto Patternmask = cmp::getBytes(Literal);
        return Patternscan::Findpattern(Range, Patternmask, Patternmask);
    }
}
