﻿/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT

    Simple verification of the utilities library.
*/

#include <Utilities/Utilities.hpp>
#include <random>

int main()
{
    printf("Running tests..\n");

    // Utilities.hpp
    [[maybe_unused]] const auto Utilitiestest = []() -> bool
    {
        // Enumeration.
        for (const auto &[Index, Item] : Enumerate({ 1U, 2U, 3U }, 1))
        {
            if (Index != Item) printf("BROKEN: Enum utility\n");
        }
        for (const auto &[Index, Item] : Enumerate({ 1U, 2U, 3U }))
        {
            if (Index != (Item - 1)) printf("BROKEN: Enum utility\n");
        }

        // Positive integers < 6..
        size_t Counter = 0;  // = { 0, 2, 4 }
        for (const auto Int : Range(0, 6, 2)) Counter += Int;
        if (Counter != 6) printf("BROKEN: Range utility\n");

        //                  = { 2, 3, 4 }
        const auto Subrange = Slice({ 1, 2, 3, 4, 5 }, 1, 4);
        if (Subrange[0] != 2 || Subrange[1] != 3 || Subrange[2] != 4)
            printf("BROKEN: Slice utility\n");

        return true;
    }();

    // Crypto/SHA.hpp
    [[maybe_unused]] const auto SHATest = []() -> bool
    {
        constexpr auto Correct256 = "5994471abb01112afcc18159f6cc74b4f511b99806da59b3caf5a9c173cacfc5";
        constexpr auto Correct512 = "3627909a29c31381a071ec27f7c9ca97726182aed29a7ddd2e54353322cfb30abb9e3a6df2ac2c20fe23436311d678564d0c8d305930575f60e2d3d048184d79";

        // Compiletime version.
        static_assert(Correct256 == Encoding::toHexstring(Hash::SHA256("12345")), "BROKEN: SHA256 hashing");
        static_assert(Correct512 == Encoding::toHexstring(Hash::SHA512("12345")), "BROKEN: SHA512 hashing");

        // Runtime version.
        if (Correct256 != Encoding::toHexstring(Hash::SHA256("12345"))) printf("BROKEN: SHA256 hashing\n");
        if (Correct512 != Encoding::toHexstring(Hash::SHA512("12345"))) printf("BROKEN: SHA512 hashing\n");

        return true;
    }();

    // Crypto/Tiger192.hpp
    [[maybe_unused]] const auto Tigertest = []() -> bool
    {
        constexpr auto Correct192 = "61d26192cf832c07612c541552d80027bbb3f520064f48ec";

        // Compiletime version.
        static_assert(Correct192 == Encoding::toHexstring(Hash::Tiger192("12345")), "BROKEN: Tiger192 hashing");

        // Runtime version.
        if (Correct192 != Encoding::toHexstring(Hash::Tiger192("12345"))) printf("BROKEN: Tiger192 hashing\n");

        return true;
    }();

    // Crypto/qDSA.hpp
    [[maybe_unused]] const auto qDSATest = []() -> bool
    {
        /*
            NOTE(tcn):
            While the implementation is constexpr compatible, it's too heavy.
            One needs to adjust the step-limit for the compiler and wait 20min.
            As there's little to no reason to ever consteval, we just do runtime checks.
        */

        std::random_device Device;
        std::mt19937_64 Engine(Device());
        std::uniform_int_distribution<uint64_t> Distribution;

        for (size_t i = 0; i < 64; ++i)
        {
            // Intentionally bad seeds for Private- and Shared-keys.
            const auto [PK1, SK1] = qDSA::Createkeypair(Distribution(Engine));
            const auto [PK2, SK2] = qDSA::Createkeypair(Distribution(Engine));

            // Key-exchange.
            const auto X1 = qDSA::Generatesecret(PK1, SK2);
            const auto X2 = qDSA::Generatesecret(PK2, SK1);
            if (X1 != X2)
            {
                printf("BROKEN: qDSA keysharing\n");
                return false;
            }

            // Signatures should be unique.
            const auto Sig1 = qDSA::Sign(PK1, SK1, cmp::stripNullchar("123"));
            const auto Sig2 = qDSA::Sign(PK2, SK2, cmp::stripNullchar("abc"));
            if (Sig1 == Sig2)
            {
                printf("BROKEN: qDSA signing\n");
                return false;
            }

            // Verify that bad signatures fail properly.
            const auto V1 = qDSA::Verify(PK1, Sig1, cmp::stripNullchar("123"));
            const auto V2 = qDSA::Verify(PK2, Sig2, cmp::stripNullchar("abc"));
            const auto V3 = qDSA::Verify(PK1, Sig2, cmp::stripNullchar("abc"));
            if (!V1 || !V2 || V3)
            {
                printf("BROKEN: qDSA verification\n");
                return false;
            }
        }

        return true;
    }();

    // Encoding/Base64.hpp
    [[maybe_unused]] const auto B64Test = []() -> bool
    {
        // Compiletime version.
        constexpr bool V1 = "MTIzNDU=" == (std::string)Base64::Encode("12345");
        constexpr bool V2 = "12345" == (std::string)Base64::Decode("MTIzNDU=");
        constexpr bool V3 = Base64::isValid("abcd") && !Base64::isValid("qrst!");
        constexpr bool V4 = "+===" == Base64::fromURL("-");
        constexpr bool V5 = "-" == Base64::toURL("+===");
        static_assert(V1 && V2, "BROKEN: Base64 Encoding");
        static_assert(V3, "BROKEN: Base64 Validation");
        static_assert(V4 && V5, "BROKEN: RFC7515");

        // Runtime version.
        if ((std::string)Base64::Decode(Base64::Encode("12345")) != "12345") printf("BROKEN: Base64 Encoding\n");
        if (!Base64::isValid("abcd") || Base64::isValid("qrst!"))printf("BROKEN: Base64 Validation\n");
        if (Base64::fromURL(Base64::toURL("+===")) != "+===") printf("BROKEN: RFC7515\n");

        return true;
    }();

    // Encoding/Base58.hpp
    [[maybe_unused]] const auto B58Test = []() -> bool
    {
        // Compiletime version.
        constexpr bool V1 = "6YvUFcg" == (std::string)Base58::Encode("12345");
        constexpr bool V2 = "12345" == (std::string)Base58::Decode("6YvUFcg");
        constexpr bool V3 = Base58::isValid("abcd") && !Base58::isValid("qrst!");
        static_assert(V1 && V2, "BROKEN: Base58 Encoding");
        static_assert(V3, "BROKEN: Base58 Validation");

        // Runtime version.
        if ((std::string)Base58::Decode(Base58::Encode("12345")) != "12345") printf("BROKEN: Base58 Encoding\n");
        if (!Base58::isValid("abcd") || Base58::isValid("qrst!"))printf("BROKEN: Base58 Validation\n");

        return true;
    }();

    // Encoding/Base85.hpp
    [[maybe_unused]] const auto B85Test = []() -> bool
    {
        // Compiletime version.
        constexpr bool T1 = "f!$Kwh2" == (std::string)Base85::Z85::Encode("12345");
        constexpr bool T2 = "12345" == (std::string)Base85::Z85::Decode("f!$Kwh2");
        constexpr bool T3 = "0etOA2#" == (std::string)Base85::ASCII85::Encode("12345");
        constexpr bool T4 = "12345" == (std::string)Base85::ASCII85::Decode("0etOA2#");
        constexpr bool T5 = "F)}kWH2" == (std::string)Base85::RFC1924::Encode("12345");
        constexpr bool T6 = "12345" == (std::string)Base85::RFC1924::Decode("F)}kWH2");

        static_assert(T1 && T2, "BROKEN: Base85::Z85 Encoding");
        static_assert(T3 && T4, "BROKEN: Base85::ASCII85 Encoding");
        static_assert(T5 && T6, "BROKEN: Base85::RFC1924 Encoding");

        // Runtime version.
        if ((std::string)Base85::Z85::Decode(Base85::Z85::Encode("12345")) != "12345") printf("BROKEN: Base85::Z85 Encoding\n");
        if ((std::string)Base85::ASCII85::Decode(Base85::ASCII85::Encode("12345")) != "12345") printf("BROKEN: Base85::ASCII85 Encoding\n");
        if ((std::string)Base85::RFC1924::Decode(Base85::RFC1924::Encode("12345")) != "12345") printf("BROKEN: Base85::RFC1924 Encoding\n");

        return true;

    }();

    // Crypto/Checksums.hpp
    [[maybe_unused]] const auto Checksumtest = []() -> bool
    {
        // Compiletime version.
        static_assert(Hash::WW32("12345") == 0xEE98FD70UL, "BROKEN: WW32 checksum");
        static_assert(Hash::FNV1_32("12345") == 0xDEEE36FAUL, "BROKEN: FNV32 checksum");
        static_assert(Hash::CRC32A("12345") == 0xCBF53A1CUL, "BROKEN: CRC32-B checksum");
        static_assert(Hash::CRC32B("12345") == 0x426548B8UL, "BROKEN: CRC32-A checksum");
        static_assert(Hash::CRC32T("12345") == 0x0315B56CUL, "BROKEN: CRC32-T checksum");
        static_assert(Hash::FNV1a_32("12345") == 0x43C2C0D8UL, "BROKEN: FNV32a checksum");
        static_assert(Hash::WW64("12345") == 0x3C570C468027DB01ULL, "BROKEN: WW64 checksum");
        static_assert(Hash::FNV1_64("12345") == 0xA92F4455DA95A77AULL, "BROKEN: FNV64 checksum");
        static_assert(Hash::FNV1a_64("12345") == 0xE575E8883C0F89F8ULL, "BROKEN: FNV64a checksum");

        // Runtime version.
        if (Hash::WW32("12345") != 0xEE98FD70UL) printf("BROKEN: WW32 checksum\n");
        if (Hash::FNV1_32("12345") != 0xDEEE36FAUL) printf("BROKEN: FNV32 checksum\n");
        if (Hash::CRC32A("12345") != 0xCBF53A1CUL) printf("BROKEN: CRC32-B checksum\n");
        if (Hash::CRC32B("12345") != 0x426548B8UL) printf("BROKEN: CRC32-A checksum\n");
        if (Hash::CRC32T("12345") != 0x0315B56CUL) printf("BROKEN: CRC32-T checksum\n");
        if (Hash::FNV1a_32("12345") != 0x43C2C0D8UL) printf("BROKEN: FNV32a checksum\n");
        if (Hash::WW64("12345") != 0x3C570C468027DB01ULL) printf("BROKEN: WW64 checksum\n");
        if (Hash::FNV1_64("12345") != 0xA92F4455DA95A77AULL) printf("BROKEN: FNV64 checksum\n");
        if (Hash::FNV1a_64("12345") != 0xE575E8883C0F89F8ULL) printf("BROKEN: FNV64a checksum\n");

        return true;
    }();

    // Encoding/JSON.hpp (mostly constexpr if __cpp_lib_variant >= 202106L)
    [[maybe_unused]] const auto JSONTest = []() -> bool
    {
        constexpr auto Input = R"({ "Object" : { "Key" : 42 }, "Array" : [ 0, 1, 2, "mixed" ] })";
        const auto Parsed = JSON::Parse(Input);
        const auto int42 = Parsed[u8"Object"][u8"Key"];
        const auto int2 = Parsed[u8"Array"][2];
        const auto Mix = Parsed[u8"Array"][3];

        const auto Test0 = uint32_t(42) == int42.Get<uint32_t>();
        const auto Test1 = uint64_t(42) == (uint64_t)int42;
        const auto Test2 = uint64_t(2) == (uint64_t)int2;
        const auto Test3 = u8"mixed"s == Mix.Get<std::u8string>();

        if (!Test0 || !Test1 || !Test2 || !Test3)
            printf("BROKEN: JSON Parsing\n");

        const auto Dump = Parsed.dump();
        if (Dump != JSON::Parse(Dump).dump())
            printf("BROKEN: JSON Parsing\n");

        return true;
    }();

    // Encoding/UTF8.hpp
    [[maybe_unused]] const auto UTF8Test = []() -> bool
    {
        // Compiletime version.
        constexpr auto V1 = L"åäö" == Encoding::toUNICODE(u8"åäö");
        constexpr auto V2 = u8"åäö" == Encoding::toUTF8("\\u00E5\\u00E4\\u00F6");
        constexpr auto V3 = "\\u00E5\\u00E4\\u00F6" == Encoding::toASCII(u8"åäö");
        constexpr auto V4 = "???" == Encoding::toASCII(Encoding::toUNICODE(u8"åäö"));
        static_assert(V1 && V2 && V3 && V4, "BROKEN: UTF8 encoding (verify that the source-file is saved as UTF8)");

        // Runtime version.
        if (L"åäö" != Encoding::toUNICODE(u8"åäö")                  ||
           (u8"åäö" != Encoding::toUTF8("\\u00E5\\u00E4\\u00F6"))   ||
           ("\\u00E5\\u00E4\\u00F6" != Encoding::toASCII(u8"åäö"))  ||
           ("???" != Encoding::toASCII(Encoding::toUNICODE(u8"åäö"))))
        {
            printf("BROKEN: UTF8 encoding (verify that the source-file is saved as UTF8)\n");
        }

        return true;
    }();

    // Encoding/Stringsplit.hpp
    [[maybe_unused]] const auto Tokenizetest = []() -> bool
    {
        // Without NULL tokens. (..., false)
        static_assert(4 == Stringsplit("ab,c,,,,,d,e", ',').size(), "BROKEN: StringsplitA");
        static_assert(4 == Stringsplit(L"ab,c,,,,,d,e", L',').size(), "BROKEN: StringsplitW");
        static_assert(4 == Stringsplit(u8"ab,c,,,,,d,e", u8',').size(), "BROKEN: StringsplitU8");

        // Preserve NULL tokens.
        static_assert(8 == Stringsplit("ab,c,,,,,d,e", ',', true).size(), "BROKEN: StringsplitA");
        static_assert(8 == Stringsplit(L"ab,c,,,,,d,e", L',', true).size(), "BROKEN: StringsplitW");
        static_assert(8 == Stringsplit(u8"ab,c,,,,,d,e", u8',', true).size(), "BROKEN: StringsplitU8");

        // Splits on ' ' and ", dropping anything in between. = { "a", "b c ", "d" }
        static_assert(3 == Tokenizestring(R"(a "b c "    "" d)").size(), "BROKEN: TokenizestringA");
        static_assert(3 == Tokenizestring(LR"(a "b c "    "" d)").size(), "BROKEN: TokenizestringW");
        static_assert(3 == Tokenizestring(u8R"(a "b c "    "" d)").size(), "BROKEN: TokenizestringU8");

        return true;
    }();

    // Containers/Ringbuffer.hpp
    [[maybe_unused]] const auto Ringbuffertest = []() -> bool
    {
        // 3 element capacity.
        Ringbuffer_t<int, 3> Buffer;
        std::array<int, 6> Rangetest{};

        // Insert 4 elements, evicts the oldest.
        Buffer.emplace_back(1);
        Buffer.emplace_back(2);
        Buffer.push_back(3);
        Buffer.push_back(4);

        if (4 != Buffer.front() || 2 != Buffer.back() || 3 != Buffer.size())
            printf("BROKEN: Ringbuffer core\n");

        // Ensure that ranges are supported by copying the 3 elements.
        std::ranges::copy(Buffer | std::views::reverse, Rangetest.begin());
        for (const auto &[Index, Value] : Enumerate(Buffer, 3)) Rangetest[Index] = Value;

        if (Rangetest != std::array{ 2, 3, 4, 4, 3, 2 })
            printf("BROKEN: Ringbuffer ranges\n");

        return true;
    }();

    // Containers/Bytebuffer.hpp
    [[maybe_unused]] const auto Bytebuffertest = []() -> bool
    {
        Bytebuffer_t Buffer{};

        Buffer << uint32_t(0x2A);
        Buffer.Write(uint8_t(2));
        Buffer.Write(uint8_t(3), false);
        Buffer << "Hello";

        const auto Dump = Buffer.to_hex();
        if ("0B 2A 00 00 00 07 02 03 02 48 65 6C 6C 6F 00"s != Dump)
            printf("BROKEN: Bytebuffer writing\n");

        // Reset the read/write iterator.
        Buffer.Rewind();

        if (42 != Buffer.Read<uint32_t>()) printf("BROKEN: Bytebuffer reading\n");
        if (2 != Buffer.Read<uint8_t>()) printf("BROKEN: Bytebuffer reading\n");
        if (3 != Buffer.Read<uint8_t>(false)) printf("BROKEN: Bytebuffer reading\n");
        if ("Hello"s != Buffer.Read<std::string>()) printf("BROKEN: Bytebuffer reading\n");

        return true;
    }();

    // Compiletime math to a reasonable accuracy of +- 0.01%.
    [[maybe_unused]] const auto Mathtest = []() -> bool
    {
        constexpr auto fPI = std::numbers::pi_v<double>;
        constexpr auto fE = std::numbers::e_v<double>;
        constexpr auto i32 = 256;

        constexpr double A[] = {
            cmp::log(fPI), cmp::log(fE), cmp::log((double)i32),
            cmp::exp(fPI), cmp::exp(fE), cmp::exp((double)i32),
            cmp::pow(fPI, 2), cmp::pow(fE, 2), cmp::pow((double)i32, 2),
            cmp::pow(fPI, 2.2), cmp::pow(fE, 2.2), cmp::pow((double)i32, 2.2)
        };
        const double B[] = {
            std::log(fPI), std::log(fE), std::log(i32),
            std::exp(fPI), std::exp(fE), std::exp(i32),
            std::pow(fPI, 2), std::pow(fE, 2), std::pow(i32, 2),
            std::pow(fPI, 2.2), std::pow(fE, 2.2), std::pow(i32, 2.2)
        };

        for (size_t i = 0; i < 12; ++i)
        {
            const auto Ratio = std::max(A[i], B[i]) / std::min(A[i], B[i]);
            const auto Percent = cmp::abs((1.0 - Ratio) * 100);

            // Can be adjusted with the cmp::Taylorsteps variable.
            if (Percent >= 0.01)
            {
                printf("CMP-math test %zu: +- %.5f %%\n", i, Percent);
            }
        }

        return true;
    }();

    printf("Testing done..\n");
    system("pause");
    return 0;
}
