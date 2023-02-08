/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-13
    License: MIT

    Z85 - Useful for embedding in sourcecode.
    RFC1924 - Useful for embedding in JSON.
    ASCII85 - Not very useful..
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Base85
{
    // We can only introduce padding for encoding.
    constexpr size_t Encodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent;
        return N * 5 / 4 + !!((N * 5 / 4) % 5);
    }
    constexpr size_t Decodesize(size_t N)
    {
        if (N == std::dynamic_extent)
            return std::dynamic_extent;
        return N * 4 / 5;
    }

    // Works on indexes.
    namespace Internal
    {
        constexpr std::array<uint32_t, 5> Pow85{ 52200625UL, 614125UL, 7225UL, 85UL, 1UL };
        constexpr auto Decodeblock(const std::array<uint8_t, 5> &Input)
        {
            uint32_t Accumulator{};

            Accumulator += (uint8_t(Input[0])) * Pow85[0];
            Accumulator += (uint8_t(Input[1])) * Pow85[1];
            Accumulator += (uint8_t(Input[2])) * Pow85[2];
            Accumulator += (uint8_t(Input[3])) * Pow85[3];
            Accumulator += (uint8_t(Input[4])) * Pow85[4];

            return cmp::toBig(Accumulator);
        }
        constexpr auto Encodeblock(uint32_t Input)
        {
            std::array<uint8_t, 5> Result{};
            Input = cmp::fromBig(Input);

            Result[0] = ((Input / Pow85[0]) % 85UL);
            Result[1] = ((Input / Pow85[1]) % 85UL);
            Result[2] = ((Input / Pow85[2]) % 85UL);
            Result[3] = ((Input / Pow85[3]) % 85UL);
            Result[4] = ((Input / Pow85[4]) % 85UL);

            return std::bit_cast<std::array<uint8_t, 5>>(Result);
        }

        // std::array at compiletime, std::basic_string at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            std::array<uint8_t, Encodesize(N)> Result{};
            const auto Remainder = N & 3;
            const auto Count = N / 4;

            for (size_t i = 0; i < Count; ++i)
            {
                uint32_t Block{};
                cmp::memcpy(&Block, &Input[i * 4], 4);
                cmp::memcpy(&Result[i * 5], Encodeblock(Block));
            }

            if (Remainder)
            {
                uint32_t Block{};
                cmp::memcpy(&Block, &Input[Count * 4], Remainder);
                cmp::memcpy(&Result[Count * 5], Encodeblock(Block).data(), Result.size() - Count * 5);
            }

            return Result;
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Encode(std::span<const T, N> Input)
        {
            std::basic_string<uint8_t> Result(Encodesize(Input.size()), T{});
            const auto Remainder = Input.size() & 3;
            const auto Count = Input.size() / 4;

            for (size_t i = 0; i < Count; ++i)
            {
                uint32_t Block{};
                cmp::memcpy(&Block, &Input[i * 4], 4);
                cmp::memcpy(&Result[i * 5], Encodeblock(Block));
            }

            if (Remainder)
            {
                uint32_t Block{};
                cmp::memcpy(&Block, &Input[Count * 4], Remainder);
                cmp::memcpy(&Result[Count * 5], Encodeblock(Block).data(), Result.size() - Count * 5);
            }

            return Result;
        }

        // std::array at compiletime, std::basic_string at runtime.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            std::array<uint8_t, Decodesize(N)> Result{};
            const auto Remainder = N % 5;
            const auto Count = N / 5;

            for (size_t i = 0; i < Count; ++i)
            {
                std::array<uint8_t, 5> Span{};
                cmp::memcpy(Span.data(), &Input[i * 5], 5);
                cmp::memcpy(&Result[i * 4], Decodeblock(Span));
            }

            if constexpr (Remainder > 0)
            {
                // For partial buffers, a placeholder value of 'u' is needed.
                std::array<uint8_t, 5> Span{ uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u') };
                cmp::memcpy(Span.data(), &Input[Count * 5], Remainder);

                const auto Block = Decodeblock(Span);
                cmp::memcpy(&Result[Count * 4], &Block, Result.size() - Count * 4);
            }

            return Result;
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Decode(std::span<const T, N> Input)
        {
            std::basic_string<uint8_t> Result(Decodesize(Input.size()), T{});
            const auto Remainder = Input.size() % 5;
            const auto Count = Input.size() / 5;

            for (size_t i = 0; i < Count; ++i)
            {
                std::array<uint8_t, 5> Span{};
                cmp::memcpy(Span.data(), &Input[i * 5], 5);
                cmp::memcpy(&Result[i * 4], Decodeblock(Span));
            }

            if (Remainder > 0)
            {
                // For partial buffers, a placeholder value of 'u' is needed.
                std::array<uint8_t, 5> Span{ uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u') };
                cmp::memcpy(Span.data(), &Input[Count * 5], Remainder);

                const auto Block = Decodeblock(Span);
                cmp::memcpy(&Result[Count * 4], &Block, Result.size() - Count * 4);
            }

            return Result;
        }

        // The output buffer has enough room to decode inplace.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N != std::dynamic_extent) constexpr auto Decode_inplace(std::span<T, N> Input)
        {
            const auto Remainder = N % 5;
            const auto Count = N / 5;

            for (size_t i = 0; i < Count; ++i)
            {
                std::array<uint8_t, 5> Span{};
                cmp::memcpy(Span.data(), &Input[i * 5], 5);
                cmp::memcpy(&Input[i * 4], Decodeblock(Span));
            }

            if constexpr (Remainder > 0)
            {
                // For partial buffers, a placeholder value of 'u' is needed.
                std::array<uint8_t, 5> Span{ uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u') };
                cmp::memcpy(Span.data(), &Input[Count * 5], Remainder);

                const auto Block = Decodeblock(Span);
                cmp::memcpy(&Input[Count * 4], &Block, Input.size() - Count * 4);
            }

            return Input.subspan(0, Decodesize(N));
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> requires(N == std::dynamic_extent) constexpr auto Decode_inplace(std::span<T, N> Input)
        {
            const auto Remainder = Input.size() % 5;
            const auto Count = Input.size() / 5;

            for (size_t i = 0; i < Count; ++i)
            {
                std::array<uint8_t, 5> Span{};
                cmp::memcpy(Span.data(), &Input[i * 5], 5);
                cmp::memcpy(&Input[i * 4], Decodeblock(Span));
            }

            if (Remainder > 0)
            {
                // For partial buffers, a placeholder value of 'u' is needed.
                std::array<uint8_t, 5> Span{ uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u'), uint8_t('u') };
                cmp::memcpy(Span.data(), &Input[Count * 5], Remainder);

                const auto Block = Decodeblock(Span);
                cmp::memcpy(&Input[Count * 4], &Block, Input.size() - Count * 4);
            }

            return Input.subspan(0, Decodesize(Input.size()));
        }
    }

    namespace Z85
    {
        constexpr auto Table = std::to_array<uint8_t>({
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
            'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
            'u', 'v', 'w', 'x', 'y', 'z',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
            'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
            'U', 'V', 'W', 'X', 'Y', 'Z',
            '.', '-', ':', '+', '=', '^', '!', '/', '*', '?',
            '&', '<', '>', '(', ')', '[', ']', '{', '}', '@',
            '%', '$', '#'
        });
        constexpr auto Reversetable = []()
        {
            std::array<uint8_t, 94> Result{};

            for (size_t i = 0; i < Table.size(); ++i)
            {
                const auto Index = uint8_t(Table[i]) - '!';
                Result[Index] = uint8_t(i);
            }

            return Result;
        }();

        // Convert between charset and index.
        template <cmp::Byte_t T, size_t N> constexpr auto toIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Reversetable[Input[i] - T{ 33 }];
            }

            return Buffer;
        }
        template <cmp::Byte_t T, size_t N> constexpr auto fromIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Table[Input[i]];
            }

            return Buffer;
        }

        // Just verify the characters, not going to bother with length.
        template <cmp::Range_t T> constexpr bool isValid(const T &Input)
        {
            constexpr auto Charset = std::string_view("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:+=^!/*?&<>()[]{}@%$#");
            return std::ranges::all_of(Input, [&](auto Char)
            {
                #if defined(__cpp_lib_string_contains)
                return Charset.contains(char(Char));
                #else
                return Charset.find(char(Char)) != Charset.end();
                #endif
            });
        }
        template <cmp::Char_t T, size_t N> constexpr bool isValid(const T(&Input)[N])
        {
            return isValid(cmp::toArray(Input));
        }

        // Dynamically selects the proper storage type.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Encode(std::span<const T, N> Input)
        {
            const auto Basic = Internal::Encode(Input);
            return cmp::Container_t<uint8_t, Encodesize(N)>(fromIndex(std::span(Basic)));
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Decode(std::span<const T, N> Input)
        {
            auto Normal = toIndex(Input);
            return cmp::Container_t<uint8_t, Decodesize(N)>(Internal::Decode_inplace(std::span(Normal)));
        }

        // Generic encoding / decoding.
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Encode(const T &Input)
        {
            return Encode(std::span(Input));
        }
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Decode(const T &Input)
        {
            return Decode(std::span(Input));
        }

        // String literal helper.
        template <cmp::Char_t T, size_t N> constexpr auto Encode(const T(&Input)[N])
        {
            return Encode(cmp::toArray(Input));
        }
        template <cmp::Char_t T, size_t N> constexpr auto Decode(const T(&Input)[N])
        {
            return Decode(cmp::toArray(Input));
        }
    }

    namespace ASCII85
    {
        // ASCII85 supports compression, while not used in our encoder, we might need to decompress it first.
        template <cmp::Byte_t T, size_t N> constexpr auto isCompressed(const std::span<const T, N> Input)
        {
            return std::ranges::any_of(Input.begin(), Input.end(), [](auto Item)
            {
                return Item == T('z') || Item == T('y');
            });
        }
        template <cmp::Byte_t T, size_t N> constexpr auto Decompress(const std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer; Buffer.reserve(Input.size() + 32);
            for (auto &Item : Input)
            {
                if (Item == T('z')) [[unlikely]] Buffer.append(4, uint8_t());
                else if (Item == T('y')) [[unlikely]] Buffer.append(4, uint8_t(' '));
                else Buffer.append(1, uint8_t(uint8_t(Item) - '!'));
            }

            return Buffer;
        }

        // Convert between charset and index.
        template <cmp::Byte_t T, size_t N> constexpr auto toIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Input[i] - T{ 33 };
            }

            return Buffer;
        }
        template <cmp::Byte_t T, size_t N> constexpr auto fromIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Input[i] + T{ 33 };
            }

            return Buffer;
        }

        // Just verify the characters, not going to bother with length.
        template <cmp::Range_t T> constexpr bool isValid(const T &Input)
        {
            return std::ranges::all_of(Input, [&](auto Char)
            {
                // 'z' and 'u' are used for compression.
                return (Char >= T('!') && Char <= T('u')) || Char == T('z') || Char == T('y');
            });
        }
        template <cmp::Char_t T, size_t N> constexpr bool isValid(const T(&Input)[N])
        {
            return isValid(cmp::toArray(Input));
        }

        // Dynamically selects the proper storage type.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Encode(std::span<const T, N> Input)
        {
            const auto Basic = Internal::Encode(Input);
            return cmp::Container_t<uint8_t, Encodesize(N)>(fromIndex(std::span(Basic)));
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Decode(std::span<const T, N> Input)
        {
            if (!std::is_constant_evaluated())
            {
                if (isCompressed(Input))
                {
                    Errorprint("ASCII85 compressed string encountered, check your inputs.");
                    return cmp::Container_t<uint8_t, Decodesize(N)>{};
                }
            }

            auto Normal = toIndex(Input);
            return cmp::Container_t<uint8_t, Decodesize(N)>(Internal::Decode_inplace(std::span(Normal)));
        }

        // Generic encoding / decoding.
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Encode(const T &Input)
        {
            return Encode(std::span(Input));
        }
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Decode(const T &Input)
        {
            return Decode(std::span(Input));
        }

        // String literal helper.
        template <cmp::Char_t T, size_t N> constexpr auto Encode(const T(&Input)[N])
        {
            return Encode(cmp::toArray(Input));
        }
        template <cmp::Char_t T, size_t N> constexpr auto Decode(const T(&Input)[N])
        {
            return Decode(cmp::toArray(Input));
        }
    }

    namespace RFC1924
    {
        constexpr auto Table = std::to_array<uint8_t>({
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
            'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
            'U', 'V', 'W', 'X', 'Y', 'Z',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
            'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
            'u', 'v', 'w', 'x', 'y', 'z',

            '!', '#', '$', '%', '&', '(', ')', '*', '+', '-',
            ';', '<', '=', '>', '?', '@', '^', '_', '`', '{',
            '|', '}', '~'
        });
        constexpr auto Reversetable = []()
        {
            cmp::Array_t<uint8_t, 94> Result{};

            for (size_t i = 0; i < Table.size(); ++i)
            {
                const auto Index = uint8_t(Table[i]) - '!';
                Result[Index] = uint8_t(i);
            }

            return Result;
        }();

        // Convert between charset and index.
        template <cmp::Byte_t T, size_t N> constexpr auto toIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Reversetable[Input[i] - T{ 33 }];
            }

            return Buffer;
        }
        template <cmp::Byte_t T, size_t N> constexpr auto fromIndex(std::span<const T, N> Input)
        {
            std::basic_string<T> Buffer(Input.size(), T{});

            for (size_t i = 0; i < Input.size(); ++i)
            {
                Buffer[i] = Table[Input[i]];
            }

            return Buffer;
        }

        // Just verify the characters, not going to bother with length.
        template <cmp::Range_t T> constexpr bool isValid(const T &Input)
        {
            constexpr auto Charset = std::string_view("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~");
            return std::ranges::all_of(Input, [&](auto Char)
            {
                #if defined(__cpp_lib_string_contains)
                return Charset.contains(char(Char));
                #else
                return Charset.find(char(Char)) != Charset.end();
                #endif
            });
        }
        template <cmp::Char_t T, size_t N> constexpr bool isValid(const T(&Input)[N])
        {
            return isValid(cmp::toArray(Input));
        }

        // Dynamically selects the proper storage type.
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Encode(std::span<const T, N> Input)
        {
            const auto Basic = Internal::Encode(Input);
            return cmp::Container_t<uint8_t, Encodesize(N)>(fromIndex(std::span(Basic)));
        }
        template <cmp::Byte_t T, size_t N = std::dynamic_extent> constexpr auto Decode(std::span<const T, N> Input)
        {
            auto Normal = toIndex(Input);
            return cmp::Container_t<uint8_t, Decodesize(N)>(Internal::Decode_inplace(std::span(Normal)));
        }

        // Generic encoding / decoding.
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Encode(const T &Input)
        {
            return Encode(std::span(Input));
        }
        template <cmp::Range_t T> requires(1 == sizeof(typename T::value_type)) constexpr auto Decode(const T &Input)
        {
            return Decode(std::span(Input));
        }

        // String literal helper.
        template <cmp::Char_t T, size_t N> constexpr auto Encode(const T(&Input)[N])
        {
            return Encode(cmp::toArray(Input));
        }
        template <cmp::Char_t T, size_t N> constexpr auto Decode(const T(&Input)[N])
        {
            return Decode(cmp::toArray(Input));
        }
    }




    // Default to RFC1924 as string encoding is mostly used in JSON.
    using namespace RFC1924;
}
