/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-06-23
    License: MIT

    Simple verification of the utilities library.
*/

#include <Utilities/Utilities.hpp>

int main()
{
    printf("Running tests..\n");

    system("calc.exe");
    LoadLibraryA("apphelp.dll");

    // Utilities.hpp
    [[maybe_unused]] const auto Utilitiestest = []() -> bool
    {
        // Zip a range with another.
        std::vector a{ 1, 2, 3 }, b{ 4, 5, 6, 7, 8, 9 }, c{ 10, 11, 12 };
        for (auto [A, B, C] : Zip(a, b, c)) { A = C; }
        if (a != c) printf("BROKEN: Zip utility\n");

        // Enumeration.
        for (const auto [Index, Item] : Enumerate({ 1, 2, 3 }, 1))
        {
            if (Index != Item) printf("BROKEN: Enum utility\n");
        }
        for (const auto [Index, Item] : Enumerate({ 1, 2, 3 }))
        {
            if (Index != (Item - 1)) printf("BROKEN: Enum utility\n");
        }

        // Positive integers < 6..
        size_t Counter = 0;  // = { 0, 2, 4 }
        for (const auto Int : Range(0, 6, 2)) Counter += Int;
        if (Counter != 6) printf("BROKEN: Range utility\n");


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

        const auto [PK1, SK1] = qDSA::Createkeypair(123);
        const auto [PK2, SK2] = qDSA::Createkeypair(321);

        const auto X1 = qDSA::Generatesecret(PK1, SK2);
        const auto X2 = qDSA::Generatesecret(PK2, SK1);
        if (X1 != X2) printf("BROKEN: qDSA keysharing\n");

        const auto Sig1 = qDSA::Sign(PK1, SK1, cmp::toArray("123"));
        const auto Sig2 = qDSA::Sign(PK2, SK2, cmp::toArray("abc"));
        if (Sig1 == Sig2) printf("BROKEN: qDSA signing\n");

        const auto V1 = qDSA::Verify(PK1, Sig1, cmp::toArray("123"));
        const auto V2 = qDSA::Verify(PK2, Sig2, cmp::toArray("abc"));
        const auto V3 = qDSA::Verify(PK1, Sig2, cmp::toArray("abc"));
        if (!V1 || !V2 || V3) printf("BROKEN: qDSA verification\n");

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
        static_assert(Hash::WW32("12345") == 0xEE98FD70UL, "BROKEN: WW32 hashing");
        static_assert(Hash::FNV1_32("12345") == 0xDEEE36FAUL, "BROKEN: FNV32 hashing");
        static_assert(Hash::CRC32A("12345") == 0xCBF53A1CUL, "BROKEN: CRC32-B hashing");
        static_assert(Hash::CRC32B("12345") == 0x426548B8UL, "BROKEN: CRC32-A hashing");
        static_assert(Hash::CRC32T("12345") == 0x0315B56CUL, "BROKEN: CRC32-T hashing");
        static_assert(Hash::FNV1a_32("12345") == 0x43C2C0D8UL, "BROKEN: FNV32a hashing");
        static_assert(Hash::WW64("12345") == 0x3C570C468027DB01ULL, "BROKEN: WW64 hashing");
        static_assert(Hash::FNV1_64("12345") == 0xA92F4455DA95A77AULL, "BROKEN: FNV64 hashing");
        static_assert(Hash::FNV1a_64("12345") == 0xE575E8883C0F89F8ULL, "BROKEN: FNV64a hashing");

        // Runtime version.
        if (Hash::WW32("12345") != 0xEE98FD70UL) printf("BROKEN: WW32 hashing\n");
        if (Hash::FNV1_32("12345") != 0xDEEE36FAUL) printf("BROKEN: FNV32 hashing\n");
        if (Hash::CRC32A("12345") != 0xCBF53A1CUL) printf("BROKEN: CRC32-B hashing\n");
        if (Hash::CRC32B("12345") != 0x426548B8UL) printf("BROKEN: CRC32-A hashing\n");
        if (Hash::CRC32T("12345") != 0x0315B56CUL) printf("BROKEN: CRC32-T hashing\n");
        if (Hash::FNV1a_32("12345") != 0x43C2C0D8UL) printf("BROKEN: FNV32a hashing\n");
        if (Hash::WW64("12345") != 0x3C570C468027DB01ULL) printf("BROKEN: WW64 hashing\n");
        if (Hash::FNV1_64("12345") != 0xA92F4455DA95A77AULL) printf("BROKEN: FNV64 hashing\n");
        if (Hash::FNV1a_64("12345") != 0xE575E8883C0F89F8ULL) printf("BROKEN: FNV64a hashing\n");

        return true;
    }();

    // Encoding/JSON.hpp
    [[maybe_unused]] const auto JSONTest = []() -> bool
    {
        constexpr auto Input = R"({ "Object" : { "Key" : 42 }, "Array" : [ 0, 1, 2, "mixed" ] })";
        const auto Parsed = JSON::Parse(Input);
        const auto int42 = Parsed["Object"]["Key"];
        const auto int2 = Parsed["Array"][2];
        const auto Mix = Parsed["Array"][3];

        const auto Test0 = uint64_t(42) == *int42.Get<uint64_t>();
        const auto Test1 = uint64_t(42) == (uint64_t)int42;
        const auto Test2 = uint64_t(2) == (uint64_t)int2;
        const auto Test3 = u8"mixed"s == *Mix.Get<std::u8string>();

        if (!Test0 || !Test1 || !Test2 || !Test3)
            printf("BROKEN: JSON Parsing\n");

        const auto Dump = Parsed.Dump();
        if (Dump != JSON::Parse(Dump).Dump())
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
        static_assert(V1 && V2 && V3 && V4, "BROKEN: UTF8 encoding (verify that the file is saved as UTF8)");

        // Runtime version.
        if (L"åäö" != Encoding::toUNICODE(u8"åäö")                  ||
           (u8"åäö" != Encoding::toUTF8("\\u00E5\\u00E4\\u00F6"))   ||
           ("\\u00E5\\u00E4\\u00F6" != Encoding::toASCII(u8"åäö"))  ||
           ("???" != Encoding::toASCII(Encoding::toUNICODE(u8"åäö"))))
        {
            printf("BROKEN: UTF8 encoding (verify that the file is saved as UTF8)\n");
        }

        return true;
    }();

    // Encoding/Stringsplit.hpp
    [[maybe_unused]] const auto Tokenizetest = []() -> bool
    {
        // Without NULL tokens. (..., false).size()
        static_assert(4 == strSplit("ab,c,,,,,d,e", ',').size(), "BROKEN: strSplitA");
        static_assert(4 == strSplit(L"ab,c,,,,,d,e", L',').size(), "BROKEN: strSplitW");

        // Preserve NULL tokens.
        static_assert(8 == strSplit("ab,c,,,,,d,e", ',', true).size(), "BROKEN: strSplitA");
        static_assert(8 == strSplit(L"ab,c,,,,,d,e", L',', true).size(), "BROKEN: strSplitW");

        // Splits on ' ' and ", dropping anything in between. = { "a", "b c ", "d" }
        static_assert(3 == Tokenizestring(R"(a "b c "    "" d)").size(), "BROKEN: TokenizestringA");
        static_assert(3 == Tokenizestring(LR"(a "b c "    "" d)").size(), "BROKEN: TokenizestringW");

        return true;
    }();

    // Containers/Ringbuffer.hpp
    [[maybe_unused]] const auto Ringbuffertest = []() -> bool
    {
        std::array<int, 6> Rangetest;
        Ringbuffer_t<int, 3> Buffer;

        // Insert 4 elements, overflows.
        Buffer.emplace_back(1);
        Buffer.emplace_back(2);
        Buffer.push_back(3);
        Buffer.push_back(4);

        // Overwrote the last element, size still 3.
        if (4 != Buffer.front() || 2 != Buffer.back() || 3 != Buffer.size())
            printf("BROKEN: Ringbuffer core\n");

        // Ensure that ranges are supported.
        for (const auto &[Index, Value] : Enumerate(Buffer | std::views::reverse)) Rangetest[Index] = Value;
        for (const auto &[Index, Value] : Enumerate(Buffer, Buffer.size())) Rangetest[Index] = Value;

        if (Rangetest != std::array{ 2, 3, 4, 4, 3, 2 })
            printf("BROKEN: Ringbuffer ranges\n");

        return true;
    }();



    printf("Testing done..\n");
    system("pause");
    return 0;
}
