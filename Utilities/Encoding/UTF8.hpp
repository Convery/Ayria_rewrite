/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-17
    License: MIT

    Simple conversions between UTF8 and ASCII / Windows Unicode.
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Encoding
{
    // The maximum for a single codepoint should be 32 bits, represented as 6 bytes of UTF8.
    using Controlcode_t = char8_t;
    using Codepoint_t = uint32_t;

    // Helper for early exits.
    template <typename T, size_t N> constexpr bool isASCII(std::span<T, N> Input)
    {
        // Covers char and char8_t; at runtime.
        if (sizeof(T) == 1 && !std::is_constant_evaluated())
        {
            const auto Intptr = (const uint32_t *)Input.data();
            const auto Remaining = Input.size() & 3;
            const auto Count32 = Input.size() / 4;

            for (size_t i = 0; i < Count32; ++i)
                if (Intptr[i] & 0x80808080)
                    return false;

            for (size_t i = 0; i < Remaining; ++i)
                if (Input[(Count32 * 4) + i] & 0x80)
                    return false;
        }

        // Covers wchar_t and char16_t; and others at compiletime.
        else
        {
            for (const auto &Item : Input)
                if (Item >= 0x80)
                    return false;
        }

        return true;
    }
    template <typename T> constexpr bool isASCII(const std::basic_string<T> &Input)
    {
        return isASCII(std::span(Input));
    }

    // Passthrough for easier templating.
    constexpr auto toASCII(std::string_view Input) { return std::string { Input.begin(), Input.end() }; }
    constexpr auto toUTF8(std::u8string_view Input) { return std::u8string { Input.begin(), Input.end() }; }
    constexpr auto toUNICODE(std::string_view Input) { return std::wstring { Input.begin(), Input.end() }; }
    constexpr auto toUNICODE(std::wstring_view Input) { return std::wstring { Input.begin(), Input.end() }; }

    // Lossy representation for ASCII.
    constexpr std::string toASCII(std::wstring_view Input)
    {
        std::string Buffer;
        Buffer.reserve(Input.size());
        for (const auto &Item : Input)
            Buffer.push_back((Item >= 0x80) ? '?' : (Item & 0x7F));

        return Buffer;
    }

    // Length needed to represent a specific codepoint.
    constexpr size_t Sequencelength(Controlcode_t Code)
    {
        if (Code == 0) [[unlikely]]
            return 0;

        if ((Code & 0x80) == 0x00) return 1;
        if ((Code & 0xE0) == 0xC0) return 2;
        if ((Code & 0xF0) == 0xE0) return 3;
        if ((Code & 0xF8) == 0xF0) return 4;
        if ((Code & 0xFC) == 0xF8) return 5;
        if ((Code & 0xFE) == 0xFC) return 6;

        // In the middle of a sequence.
        if ((Code & 0xC0) == 0x80) [[unlikely]]
        {
            if (!std::is_constant_evaluated())
                assert(false);
        }

        return 0;
    }
    constexpr size_t Sequencelength(Codepoint_t Code)
    {
        if (Code < 0x80) return 1;
        if (Code < 0x800) return 2;
        if (Code < 0x10000) return 3;
        if (Code < 0x200000) return 4;
        if (Code < 0x4000000) return 5;
        if (Code < 0x80000000) return 6;

        // Fallback.
        return 0;
    }

    // Conversion between a 32-bit codepoint and a <= 6 byte sequence.
    constexpr Codepoint_t toCodepoint(std::u8string_view Sequence)
    {
        const auto Wantedsize = Sequencelength(Sequence.front());
        if (!std::is_constant_evaluated()) assert(Wantedsize);

        if (!Wantedsize || (Wantedsize > Sequence.size())) return 0;

        if (Wantedsize == 1) return static_cast<Codepoint_t>(Sequence[0]);

        if (Wantedsize == 2) return static_cast<Codepoint_t>( ((Sequence[0] & 0x1F) << 6)  | (Sequence[1] & 0x3F));

        if (Wantedsize == 3) return static_cast<Codepoint_t>( ((Sequence[0] & 0x0F) << 12) | ((Sequence[1] & 0x3F) << 6)
                                                             | (Sequence[2] & 0x3F));

        if (Wantedsize == 4) return static_cast<Codepoint_t>(  ((Sequence[0] & 0x07) << 18) | ((Sequence[1] & 0x3F) << 12)
                                                             | ((Sequence[2] & 0x3F) << 6)  | (Sequence[3] & 0x3F));

        if (Wantedsize == 5) return static_cast<Codepoint_t>(  ((Sequence[0] & 0x03) << 24) | ((Sequence[1] & 0x3F) << 18)
                                                             | ((Sequence[2] & 0x3F) << 12) | ((Sequence[3] & 0x3F) << 6)
                                                             | (Sequence[4] & 0x3F));

        if (Wantedsize == 6) return static_cast<Codepoint_t>(  ((Sequence[0] & 0x01) << 30) | ((Sequence[1] & 0x3F) << 24)
                                                             | ((Sequence[2] & 0x3F) << 18) | ((Sequence[3] & 0x3F) << 12)
                                                             | ((Sequence[4] & 0x3F) << 6)  | (Sequence[5] & 0x3F));

        // Impossible case.
        return 1;
    }
    constexpr Codepoint_t toCodepoint(std::wstring_view Escape)
    {
        // atoi
        constexpr auto fromHex = [](std::wstring_view Input)
        {
            Codepoint_t Result{};

            for (const auto &Item : Input)
            {
                Result <<= 4;

                if (Item > 0x40 && Item < 0x47) { Result |= (Item - 0x37); }
                if (Item > 0x60 && Item < 0x67) { Result |= (Item - 0x57); }
                if (Item > 0x2F && Item < 0x3A) { Result |= (Item - 0x30); }
            }

            return Result;
        };

        // Cleanup.
        while (Escape.front() == L'\\') Escape.remove_prefix(1);

        // Escapes should either be in the format of \uXXXX or \U00XXXXXX
        if (Escape.front() == L'u')
        {
            Escape.remove_prefix(1);
            return fromHex(Escape.substr(0, 4));
        }
        else if (Escape.front() == L'U')
        {
            Escape.remove_prefix(1);
            return fromHex(Escape.substr(0, 8));
        }

        if (!std::is_constant_evaluated()) assert(false);
        return 0;
    }
    constexpr Codepoint_t toCodepoint(std::string_view Escape)
    {
        // atoi
        constexpr auto fromHex = [](std::string_view Input)
        {
            Codepoint_t Result{};

            for (const auto &Item : Input)
            {
                Result <<= 4;

                if (Item > 0x40 && Item < 0x47) { Result |= (Item - 0x37); }
                if (Item > 0x60 && Item < 0x67) { Result |= (Item - 0x57); }
                if (Item > 0x2F && Item < 0x3A) { Result |= (Item - 0x30); }
            }

            return Result;
        };

        // Cleanup.
        while (Escape.front() == L'\\') Escape.remove_prefix(1);

        // Escapes should either be in the format of \uXXXX or \U00XXXXXX
        if (Escape.front() == 'u')
        {
            Escape.remove_prefix(1);
            return fromHex(Escape.substr(0, 4));
        }
        else if (Escape.front() == 'U')
        {
            Escape.remove_prefix(1);
            return fromHex(Escape.substr(0, 8));
        }

        if (!std::is_constant_evaluated()) assert(false);
        return 0;
    }
    constexpr std::u8string fromCodepoint(Codepoint_t Code)
    {
        const auto Size = Sequencelength(Code);
        std::u8string Result{};
        Result.reserve(Size);

        // Invalid codepoint.
        if (!std::is_constant_evaluated()) assert(Size);
        if (Size == 0) [[unlikely]]
            return Result;

        do
        {
            // 8-bit.
            if (Code < 0x80) [[likely]]
            {
                Result.push_back(static_cast<char8_t>(Code));
                break;
            }

            // 11-bit.
            if (Code < 0x800) [[likely]]
            {
                Result.push_back(static_cast<char8_t>(((Code & 0x7C0) >> 6) | 0xC0));
                Result.push_back(static_cast<char8_t>((Code & 0x3F) | 0x80));
                break;
            }

            // 16-bit.
            if (Code < 0x10000) [[likely]]
            {
                Result.push_back(static_cast<char8_t>(((Code & 0xF000) >> 12) | 0xE0));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC0) >> 6) | 0x80));
                Result.push_back(static_cast<char8_t>((Code & 0x3F) | 0x80));
                break;
            }

            // 21-bit.
            if (Code < 0x200000) [[unlikely]]
            {
                Result.push_back(static_cast<char8_t>(((Code & 0x1C0000) >> 18) | 0xF0));
                Result.push_back(static_cast<char8_t>(((Code & 0x3F000) >> 12) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC0) >> 6) | 0x80));
                Result.push_back(static_cast<char8_t>((Code & 0x3F) | 0x80));
                break;
            }

            // 26-bit.
            if (Code < 0x4000000) [[unlikely]]
            {
                Result.push_back(static_cast<char8_t>(((Code & 0x3000000) >> 24) | 0xF8));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC000) >> 18) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0x3F000) >> 12) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC0) >> 6) | 0x80));
                Result.push_back(static_cast<char8_t>((Code & 0x3F) | 0x80));
                break;
            }

            // 30-bit.
            if (Code < 0x80000000) [[unlikely]]
            {
                Result.push_back(static_cast<char8_t>(((Code & 0x40000000) >> 30) | 0xFC));
                Result.push_back(static_cast<char8_t>(((Code & 0x3F000000) >> 24) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC000) >> 18) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0x3F000) >> 12) | 0x80));
                Result.push_back(static_cast<char8_t>(((Code & 0xFC0) >> 6) | 0x80));
                Result.push_back(static_cast<char8_t>((Code & 0x3F) | 0x80));
                break;
            }

        } while (false);

        return Result;
    }

    // Escape a codepoint to be suitable for embedding.
    template <bool asUTF16 = false> constexpr std::string narrowPoint(Codepoint_t Code)
    {
        constexpr auto toHex = [](uint8_t Byte) -> std::string
        {
            constexpr char Charset[] = "0123456789ABCDEF";
            return std::string(1, Charset[Byte >> 4]) + Charset[Byte & 7];
        };

        const auto Bytes = std::bit_cast<std::array<uint8_t, 4>>(cmp::toBig(Code));

        if (!(Code & 0xFFFF0000))
        {
            return std::string("\\u") + toHex(Bytes[2]) + toHex(Bytes[3]);
        }
        else
        {
            if constexpr (asUTF16)
            {
                return narrowPoint<false>(0xD7C0U + (Code >> 10)) + narrowPoint<false>(0xDC00U + (Code & 0x3FF));
            }
            else
            {
                return std::string("\\U") + toHex(Bytes[0]) + toHex(Bytes[1]) + toHex(Bytes[2]) + toHex(Bytes[3]);
            }
        }
    }
    template <bool asUTF16 = false> constexpr std::wstring widePoint(Codepoint_t Code)
    {
        return toUNICODE(narrowPoint<asUTF16>(Code));
    }

    // We do not validate sequences.
    constexpr std::wstring toUNICODE(std::u8string_view Input)
    {
        std::wstring Buffer{};
        Buffer.reserve(Input.size() * 2);

        while (!Input.empty())
        {
            const auto Codepoint = toCodepoint(Input);
            const auto Size = Sequencelength(Codepoint);
            if (!std::is_constant_evaluated()) { assert(Codepoint); assert(Size); }
            if (Size == 0 || Codepoint == 0) [[unlikely]] return Buffer;

            // Single-byte sequence is ASCII (common case).
            if (Size > 3) [[unlikely]] Buffer.append(widePoint(Codepoint));
            else Buffer.push_back(static_cast<wchar_t>(Codepoint));

            Input.remove_prefix(Size);
        }

        return Buffer;
    }
    constexpr std::string toASCII(std::u8string_view Input)
    {
        // Most common case, pure ASCII.
        if (isASCII(std::span(Input))) [[likely]]
        {
            return { Input.begin(), Input.end() };
        }

        std::string Buffer{};
        Buffer.reserve(Input.size() * 2);

        while (!Input.empty())
        {
            const auto Codepoint = toCodepoint(Input);
            const auto Size = Sequencelength(Codepoint);
            if (!std::is_constant_evaluated()) { assert(Codepoint); assert(Size); }
            if (Size == 0) [[unlikely]] return Buffer;

            // Single-byte sequence is ASCII (common case).
            if (Size != 1) [[unlikely]] Buffer.append(narrowPoint(Codepoint));
            else Buffer.push_back(std::bit_cast<char>(Input.front()));

            Input.remove_prefix(Size);
        }

        return Buffer;
    }
    constexpr std::u8string toUTF8(std::wstring_view Input)
    {
        std::u8string Result{};
        Result.reserve(Input.size() * 3);

        // Common case is that it's only ASCII data.
        if (std::min(Input.find(L"\\u"), Input.find(L"\\U")) == std::wstring_view::npos) [[likely]]
        {
            for (const auto &Item : Input) Result.append(fromCodepoint(Item));
            return Result;
        }

        // In case of extended 32-bit codepoints.
        Codepoint_t Extendedpoint{};

        // Process to the end.
        while (!Input.empty())
        {
            const auto Point = std::min(Input.find(L"\\u"), Input.find(L"\\U"));
            const auto Unicode = Input.substr(0, Point);

            // Append the Unicode part..
            for (const auto &Item : Unicode) Result.append(fromCodepoint(Item));
            Input.remove_prefix(Unicode.size());

            // No result found.
            if (Point == std::string_view::npos) break;

            // Decode the escape-sequence.
            const auto Codepoint = toCodepoint(Input);
            if (!std::is_constant_evaluated()) assert(Codepoint);
            if (!(Codepoint & 0xFFFF0000)) Input.remove_prefix(6);
            else Input.remove_prefix(10);

            // Unlikely UTF16 continuation sequence.
            if (0xD800 <= Codepoint && Codepoint <= 0xDBFF) [[unlikely]]
            {
                Extendedpoint = Codepoint << 10;
            }
            else
            {
                // Unlikely UTF16 continuation sequence.
                if (Extendedpoint) [[unlikely]]
                {
                    Extendedpoint += Codepoint;
                    Extendedpoint -= 0x35FDC00U;
                    Result.append(fromCodepoint(Extendedpoint));
                    Extendedpoint = 0;
                }
                else
                {
                    Result.append(fromCodepoint(Codepoint));
                }
            }
        }

        // Malformed control sequence, characters have been dropped.
        if (!std::is_constant_evaluated()) assert(Extendedpoint == 0);

        return Result;


    }
    constexpr std::u8string toUTF8(std::string_view Input)
    {
        // Common case is that it's only ASCII data.
        if (std::min(Input.find("\\u"), Input.find("\\U")) == std::string_view::npos) [[likely]]
        {
            return std::u8string(Input.begin(), Input.end());
        }

        // Common case is ASCII with the code-points being smaller than text.
        std::u8string Result{}; Result.reserve(Input.size() * sizeof(char));

        // In case of extended 32-bit codepoints.
        Codepoint_t Extendedpoint{};

        // Process to the end.
        while (!Input.empty())
        {
            const auto Point = std::min(Input.find("\\u"), Input.find("\\U"));
            const auto ASCII = Input.substr(0, Point);

            // Append the ASCII part as is.
            Result.append(ASCII.begin(), ASCII.end());
            Input.remove_prefix(ASCII.size());

            // No result found.
            if (Point == std::string_view::npos) break;

            // Decode the escape-sequence.
            const auto Codepoint = toCodepoint(Input);
            if (!std::is_constant_evaluated()) assert(Codepoint);
            if (!(Codepoint & 0xFFFF0000)) Input.remove_prefix(6);
            else Input.remove_prefix(10);

            // Unlikely UTF16 continuation sequence.
            if (0xD800 <= Codepoint && Codepoint <= 0xDBFF) [[unlikely]]
            {
                Extendedpoint = Codepoint << 10;
            }
            else
            {
                // Unlikely UTF16 continuation sequence.
                if (Extendedpoint) [[unlikely]]
                {
                    Extendedpoint += Codepoint;
                    Extendedpoint -= 0x35FDC00U;
                    Result.append(fromCodepoint(Extendedpoint));
                    Extendedpoint = 0;
                }
                else
                {
                    Result.append(fromCodepoint(Codepoint));
                }
            }
        }

        // Malformed control sequence, characters have been dropped.
        assert(Extendedpoint == 0);

        return Result;
    }
}

// Utilities for UTF8 strings.
namespace UTF8
{
    constexpr size_t strlen(std::u8string_view Input)
    {
        if (Encoding::isASCII(std::span(Input))) [[likely]]
            return Input.size();

        size_t Size{};
        for (auto it = Input.begin(); it != Input.end();)
        {
            const auto Length = Encoding::Sequencelength(*it);

            if (!std::is_constant_evaluated()) assert(Length);
            if (Length == 0) [[unlikely]]
                return Size;

            it += Length;
            Size++;
        }

        return Size;
    }
    constexpr size_t Offset(std::u8string_view Input, size_t Index)
    {
        for (auto it = Input.begin(); it != Input.end();)
        {
            const auto Length = Encoding::Sequencelength(*it);

            if (!std::is_constant_evaluated()) assert(Length);
            if (Length == 0) [[unlikely]]
                return Input.size();

            if (Index-- > 0) it += Length;
            else return it - Input.begin();
        }

        return Input.size();
    }

    constexpr std::u8string::iterator at(std::u8string &Input, size_t Index)
    {
        for (auto it = Input.begin(); it != Input.end();)
        {
            const auto Length = Encoding::Sequencelength(*it);

            if (!std::is_constant_evaluated()) assert(Length);
            if (Length == 0) [[unlikely]]
                return Input.end();

            if (Index-- > 0) it += Length;
            else return it;
        }

        return Input.end();
    }
    constexpr std::u8string_view::iterator at(std::u8string_view Input, size_t Index)
    {
        for (auto it = Input.begin(); it != Input.end();)
        {
            const auto Length = Encoding::Sequencelength(*it);

            if (!std::is_constant_evaluated()) assert(Length);
            if (Length == 0) [[unlikely]]
                return Input.end();

            if (Index-- > 0) it += Length;
            else return it;
        }

        return Input.end();
    }
    constexpr std::u8string_view substr(std::u8string_view Input, size_t Start, size_t Stop)
    {
        const auto pStop = Offset(Input, Stop);
        const auto pStart = Offset(Input, Start);
        if (pStart == pStop) [[unlikely]] return {};

        return { &Input[pStart], pStop - pStart };
    }
}
