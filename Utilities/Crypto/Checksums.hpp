/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-17
    License: MIT

    Non-cryptographic hashes focusing on speed.
*/

#pragma once
#include <Utilities/Utilities.hpp>

namespace Hash
{
    namespace Checksums
    {
        #pragma region FNV1
        constexpr uint64_t FNV1_Offset_64 = 14695981039346656037ULL;
        constexpr uint64_t FNV1_Prime_64 = 1099511628211ULL;
        constexpr uint32_t FNV1_Offset_32 = 2166136261UL;
        constexpr uint32_t FNV1_Prime_32 = 16777619UL;

        constexpr uint32_t FNV1_32(std::span<const uint8_t> Input)
        {
            auto Hash = FNV1_Offset_32;

            for (const auto Item : Input)
            {
                Hash *= FNV1_Prime_32;
                Hash ^= uint8_t(Item);
            }

            return Hash;
        }
        constexpr uint64_t FNV1_64(std::span<const uint8_t> Input)
        {
            auto Hash = FNV1_Offset_64;

            for (const auto Item : Input)
            {
                Hash *= FNV1_Prime_64;
                Hash ^= uint8_t(Item);
            }

            return Hash;
        }
        constexpr uint32_t FNV1a_32(std::span<const uint8_t> Input)
        {
            auto Hash = FNV1_Offset_32;

            for (const auto Item : Input)
            {
                Hash ^= uint8_t(Item);
                Hash *= FNV1_Prime_32;
            }

            return Hash;
        }
        constexpr uint64_t FNV1a_64(std::span<const uint8_t> Input)
        {
            auto Hash = FNV1_Offset_64;

            for (const auto Item : Input)
            {
                Hash ^= uint8_t(Item);
                Hash *= FNV1_Prime_64;
            }

            return Hash;
        }

        #pragma endregion

        #pragma region WW
        constexpr uint64_t _wheatp0 = 0xA0761D6478BD642FULL, _wheatp1 = 0xE7037ED1A0B428DBULL, _wheatp2 = 0x8EBC6AF09C88C6E3ULL;
        constexpr uint64_t _wheatp3 = 0x589965CC75374CC3ULL, _wheatp4 = 0x1D8E4E27C47D124FULL, _wheatp5 = 0xEB44ACCAB455D165ULL;
        constexpr uint64_t _waterp0 = 0xA0761D65ULL, _waterp1 = 0xE7037ED1ULL, _waterp2 = 0x8EBC6AF1ULL;
        constexpr uint64_t _waterp3 = 0x589965CDULL, _waterp4 = 0x1D8E4E27ULL, _waterp5 = 0xEB44ACCBULL;

        template <size_t Bits, cmp::Byte_t T> constexpr uint64_t toINT64(const T *p)
        {
            uint64_t Result{};

            if constexpr (Bits >= 64) Result |= (uint64_t(*p++) << (Bits - 64));
            if constexpr (Bits >= 56) Result |= (uint64_t(*p++) << (Bits - 56));
            if constexpr (Bits >= 48) Result |= (uint64_t(*p++) << (Bits - 48));
            if constexpr (Bits >= 40) Result |= (uint64_t(*p++) << (Bits - 40));
            if constexpr (Bits >= 32) Result |= (uint64_t(*p++) << (Bits - 32));
            if constexpr (Bits >= 24) Result |= (uint64_t(*p++) << (Bits - 24));
            if constexpr (Bits >= 16) Result |= (uint64_t(*p++) << (Bits - 16));
            if constexpr (Bits >= 8)  Result |= (uint64_t(*p  ) << (Bits - 8));
            return Result;
        }
        constexpr uint64_t WWProcess(uint64_t A, uint64_t B)
        {
            const uint64_t Tmp{ A * B };
            return Tmp - (Tmp >> 32);
        }

        constexpr uint32_t WW32(std::span<const uint8_t> Input)
        {
            const auto Remainder = Input.size() & 15;
            const auto Count = Input.size() / 16;
            uint64_t Hash = _waterp0;

            for (size_t i = 0; i < Count; ++i)
            {
                const auto Offset = Input.data() + i * 16;
                const auto P1 = WWProcess(toINT64<32>(Offset) ^ _waterp1, toINT64<32>(Offset + 4) ^ _waterp2);
                const auto P2 = WWProcess(toINT64<32>(Offset + 8) ^ _waterp3, toINT64<32>(Offset + 12) ^ _waterp4);

                Hash = WWProcess(P1 + Hash, P2);
            }
            Hash += _waterp5;

            const auto Offset = Input.data() + Count * 16;
            switch (Remainder)
            {
                case 0:  break;
                case 1:  Hash = WWProcess(_waterp2 ^ Hash, toINT64<8>(Offset) ^ _waterp1); break;
                case 2:  Hash = WWProcess(_waterp3 ^ Hash, toINT64<16>(Offset) ^ _waterp4); break;
                case 3:  Hash = WWProcess(toINT64<16>(Offset) ^ Hash, toINT64<8>(Offset + 2) ^ _waterp2); break;
                case 4:  Hash = WWProcess(toINT64<16>(Offset) ^ Hash, toINT64<16>(Offset + 2) ^ _waterp3); break;
                case 5:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<8>(Offset + 4) ^ _waterp1); break;
                case 6:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<16>(Offset + 4) ^ _waterp1); break;
                case 7:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, ((toINT64<16>(Offset + 4) << 8) | toINT64<8>(Offset + 6)) ^ _waterp1); break;
                case 8:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp0); break;
                case 9:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash ^ _waterp4, toINT64<8>(Offset + 8) ^ _waterp3); break;
                case 10: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash, toINT64<16>(Offset + 8) ^ _waterp3); break;
                case 11: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash, ((toINT64<16>(Offset + 8) << 8) | toINT64<8>(Offset + 10)) ^ _waterp3); break;
                case 12: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), _waterp4); break;
                case 13: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<8>(Offset + 12)) ^ _waterp4); break;
                case 14: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<16>(Offset + 12)) ^ _waterp4); break;
                case 15: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _waterp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<16>(Offset + 12) << 8 | toINT64<8>(Offset + 14)) ^ _waterp4); break;
            }

            Hash = (Hash ^ (Hash << 16)) * (uint64_t(Input.size()) ^ _waterp0);
            return (uint32_t)(Hash - (Hash >> 32));
        }
        constexpr uint64_t WW64(std::span<const uint8_t> Input)
        {
            const auto Remainder = Input.size() & 15;
            const auto Count = Input.size() / 16;
            uint64_t Hash = _wheatp0;

            for (size_t i = 0; i < Count; ++i)
            {
                const auto Offset = Input.data() + i * 16;
                const auto P1 = WWProcess(toINT64<32>(Offset) ^ _wheatp1, toINT64<32>(Offset + 4) ^ _wheatp2);
                const auto P2 = WWProcess(toINT64<32>(Offset + 8) ^ _wheatp3, toINT64<32>(Offset + 12) ^ _wheatp4);

                Hash = WWProcess(P1 + Hash, P2);
            }
            Hash += _wheatp5;

            const auto Offset = Input.data() + Count * 16;
            switch (Remainder)
            {
                case 0:  break;
                case 1:  Hash = WWProcess(_wheatp2 ^ Hash, toINT64<8>(Offset) ^ _wheatp1); break;
                case 2:  Hash = WWProcess(_wheatp3 ^ Hash, toINT64<16>(Offset) ^ _wheatp4); break;
                case 3:  Hash = WWProcess(toINT64<16>(Offset) ^ Hash, toINT64<8>(Offset + 2) ^ _wheatp2); break;
                case 4:  Hash = WWProcess(toINT64<16>(Offset) ^ Hash, toINT64<16>(Offset + 2) ^ _wheatp3); break;
                case 5:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<8>(Offset + 4) ^ _wheatp1); break;
                case 6:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<16>(Offset + 4) ^ _wheatp1); break;
                case 7:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, ((toINT64<16>(Offset + 4) << 8) | toINT64<8>(Offset + 6)) ^ _wheatp1); break;
                case 8:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp0); break;
                case 9:  Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash ^ _wheatp4, toINT64<8>(Offset + 8) ^ _wheatp3); break;
                case 10: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash, toINT64<16>(Offset + 8) ^ _wheatp3); break;
                case 11: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash, ((toINT64<16>(Offset + 8) << 8) | toINT64<8>(Offset + 10)) ^ _wheatp3); break;
                case 12: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), _wheatp4); break;
                case 13: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<8>(Offset + 12)) ^ _wheatp4); break;
                case 14: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<16>(Offset + 12)) ^ _wheatp4); break;
                case 15: Hash = WWProcess(toINT64<32>(Offset) ^ Hash, toINT64<32>(Offset + 4) ^ _wheatp2) ^ WWProcess(Hash ^ toINT64<32>(Offset + 8), (toINT64<16>(Offset + 12) << 8 | toINT64<8>(Offset + 14)) ^ _wheatp4); break;
            }

            Hash = (Hash ^ Hash << 16) * (uint64_t(Input.size()) ^ _wheatp0);
            return Hash - (Hash >> 31) + (Hash << 33);
        }

        #pragma endregion

        #pragma region CRC
        template <uint32_t Polynomial, bool Shiftright>
        constexpr uint32_t CRC32(std::span<const uint8_t> Input, uint32_t IV)
        {
            uint32_t CRC = IV;

            for (const auto Item : Input)
            {
                if constexpr (Shiftright) CRC ^= uint8_t(Item);
                else CRC ^= uint8_t(Item) << 24;

                for (size_t b = 0; b < 8; ++b)
                {
                    if constexpr (Shiftright)
                    {
                        if (!(CRC & 1)) CRC >>= 1;
                        else CRC = (CRC >> 1) ^ Polynomial;
                    }
                    else
                    {
                        if (!(CRC & (1UL << 31))) CRC <<= 1;
                        else CRC = (CRC << 1) ^ Polynomial;
                    }
                }
            }

            return ~CRC;
        }


        // IEEE CRC32
        constexpr uint32_t CRC32A(std::span<const uint8_t> Input)
        {
            return CRC32<0xEDB88320, true>(Input, 0xFFFFFFFF);
        }

        // BZIP2 CRC32
        constexpr uint32_t CRC32B(std::span<const uint8_t> Input)
        {
            return CRC32<0x04C11DB7, false>(Input, 0xFFFFFFFF);
        }

        // Tencent CRC32
        constexpr uint32_t CRC32T(std::span<const uint8_t> Input)
        {
            return CRC32<0xEDB88320, true>(Input, ~uint32_t(Input.size()));
        }

        #pragma endregion

        // Forward to the internal hashes.
        template <typename F, cmp::Range_t T> constexpr auto doHash(F &&Func, const T &Input)
        {
            return Func(cmp::getBytes(Input));
        }
        template <typename F, typename T> requires (!cmp::Range_t<T>) constexpr auto doHash(F &&Func, const T &Input)
        {
            return Func(cmp::getBytes(Input));
        }
        template <typename F, typename T> requires (!cmp::Range_t<T>) constexpr auto doHash(F &&Func, const T *Data, size_t Size)
        {
            return Func(cmp::getBytes(std::span(Data, Size)));
        }
        template <typename F, cmp::Char_t T, size_t N> requires (!cmp::Range_t<T>) constexpr auto doHash(F &&Func, const T(&Input)[N])
        {
            return Func(cmp::getBytes(cmp::toArray(Input)));
        }
    }

    #define Impl(x) template <typename ...Args> [[nodiscard]] constexpr decltype(auto) x (Args&& ...va) \
    { return Checksums::doHash([](auto...X){ return Checksums:: x(X...); }, std::forward<Args>(va)...); }

    Impl(FNV1_32); Impl(FNV1_64);
    Impl(FNV1a_32); Impl(FNV1a_64);
    Impl(WW32); Impl(WW64);
    Impl(CRC32A); Impl(CRC32B); Impl(CRC32T);
}

// Drop-in generic functions for std:: algorithms, containers, and such.
// e.g. std::unordered_set<SillyType, decltype(X::Hash), decltype(X::Equal)>
namespace FNV32
{
    constexpr auto Hash = [](const auto &v) {return Hash::FNV1a_32(v); };
    constexpr auto Equal = [](const auto &l, const auto &r) { return Hash(l) == Hash(r); };
}
namespace FNV64
{
    constexpr auto Hash = [](const auto &v) {return Hash::FNV1a_64(v); };
    constexpr auto Equal = [](const auto &l, const auto &r) { return Hash(l) == Hash(r); };
}
namespace WW32
{
    constexpr auto Hash = [](const auto &v) {return Hash::WW32(v); };
    constexpr auto Equal = [](const auto &l, const auto &r) { return Hash(l) == Hash(r); };
}
namespace WW64
{
    constexpr auto Hash = [](const auto &v) {return Hash::WW64(v); };
    constexpr auto Equal = [](const auto &l, const auto &r) { return Hash(l) == Hash(r); };
}
