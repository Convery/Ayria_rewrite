/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2024-03-03
    License: MIT

    quotient Digital Signature Algorithm - arXiv:1709.03358
    Hardening - 10.46586/tches.v2018.i3.331-371

    qDSA is implemented over the Kummer-variety Genus 2 curve.
    Using Gaudry-Schost rather than Schnorr signatures to counter Bleichenbacher attacks.
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include <Utilities/Crypto/SHA.hpp>

// API definition.
namespace qDSA
{
    // Fixed size containers for primitive types.
    using Privatekey_t = std::array<uint8_t, 32>;
    using Publickey_t = std::array<uint8_t, 32>;
    using Sharedkey_t = std::array<uint8_t, 32>;
    using Signature_t = std::array<uint8_t, 64>;

    // Create a keypair from a random seed.
    constexpr Publickey_t getPublickey(const Privatekey_t &Privatekey);
    template <typename T> constexpr std::pair<Publickey_t, Privatekey_t> Createkeypair(const T &Seed);

    // Compute a shared secret between two keypairs (A.PK, B.SK) == (B.PK, A.SK)
    constexpr std::optional<Sharedkey_t> Generatesecret(const Publickey_t &Publickey, const Privatekey_t &Privatekey);

    // Create a signature for the provided message, somewhat hardened against hackery.
    template <cmp::Sequential_t C> constexpr Signature_t Sign(const Publickey_t &Publickey, const Privatekey_t &Privatekey, const C &Message);

    // Verify that the message was signed by the owner of the public key.
    template <cmp::Sequential_t C> constexpr bool Verify(const Publickey_t &Publickey, const Signature_t &Signature, const C &Message);

    // Combine arrays, for cleaner code.
    template <typename T, size_t N, size_t M>
    constexpr auto operator+(const std::array<T, N> &Left, const std::array<T, M> &Right)
    {
        return[]<size_t... LIndex, size_t... RIndex>(const std::array<T, N> &Left, const std::array<T, M> &Right, std::index_sequence<LIndex...>, std::index_sequence<RIndex...>)
        {
            return std::array<T, N + M>{{Left[LIndex]..., Right[RIndex]...}};
        }(Left, Right, std::make_index_sequence<N>(), std::make_index_sequence<M>());
    }
}

// We need some larger integers, but in a separate namespace so that there's no collision with native uint128_t.
namespace qDSA::Detail
{
    template <size_t Bits> struct Bigint_t : std::array<uint64_t, Bits / 64>
    {
        static_assert(Bits % 64 == 0, "Size must be a multiple of 64 bits");
        using Base_t = std::array<uint64_t, Bits / 64>;
        using Base_t::value_type;
        using Base_t::operator=;
        using Base_t::array;

        constexpr Bigint_t() = default;
        constexpr Bigint_t(Bigint_t<Bits> &&) = default;
        constexpr Bigint_t(const Bigint_t<Bits> &) = default;
        constexpr Bigint_t(uint64_t Low) : Base_t({ Low }) {}
        constexpr Bigint_t(const Base_t &Array) : Base_t(Array) {}
        constexpr Bigint_t(Base_t &&Array) : Base_t(std::move(Array)) {}
        constexpr Bigint_t(const std::array<uint64_t, 1> &Single) : Base_t({ Single[0] }) {}
        constexpr Bigint_t(std::initializer_list<uint64_t> List) : Base_t()
        {
            std::copy(List.begin(), List.end(), this->begin());
        }

        // Implicitly deleted..
        Bigint_t &operator=(Bigint_t &&) = default;
        Bigint_t &operator=(const Bigint_t &) = default;

        //
        static constexpr size_t size() noexcept
        {
            return Bits / 64;
        }

        // Helper for Bigint_t <-> Bigint_t construction.
        constexpr Bigint_t(const Bigint_t<128> &Low, const Bigint_t<128> &High) requires(Bits == 256) : Bigint_t(Low + High) {}
        constexpr Bigint_t(const Bigint_t<256> &Low, const Bigint_t<256> &High) requires(Bits == 512) : Bigint_t(Low + High) {}

        // Endian-safe access incase of bit_casts.
        constexpr auto &operator[](size_t Index)
        {
            if (std::endian::native == std::endian::little)
                return std::array<uint64_t, Bits / 64>::operator[](Index);
            else
                return std::array<uint64_t, Bits / 64>::operator[](this->size() - 1 - Index);
        }
        constexpr auto &operator[](size_t Index) const
        {
            if (std::endian::native == std::endian::little)
                return std::array<uint64_t, Bits / 64>::operator[](Index);
            else
                return std::array<uint64_t, Bits / 64>::operator[](this->size() - 1 - Index);
        }

        // Helpers for access as elements.
        constexpr auto asPair() const requires(Bits == 128)
        {
            return std::tuple<uint64_t, uint64_t>{ this->operator[](0), this->operator[](1) };
        }
        constexpr auto asPair() const requires(Bits == 256)
        {
            return std::tuple<Bigint_t<128>, Bigint_t<128>>{
                {this->operator[](0), this->operator[](1)}, { this->operator[](2), this->operator[](3) }
            };
        }
        constexpr auto asPair() const requires(Bits == 512)
        {
            return std::tuple<Bigint_t<256>, Bigint_t<256>>{
                { this->operator[](0), this->operator[](1), this->operator[](2), this->operator[](3) },
                { this->operator[](4), this->operator[](5), this->operator[](6), this->operator[](7) }
            };
        }
        constexpr std::array<uint8_t, Bits / 8> asBytes() const
        {
            return std::bit_cast<std::array<uint8_t, Bits / 8>>(*this);
        }

        friend constexpr bool operator==(const Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right)
        {
            bool Result = true;

            for (uint8_t i = 0; i < Left.size(); ++i)
                Result &= (Left[i] == Right[i]);

            return Result;
        }

        friend constexpr Bigint_t<Bits> operator~(const Bigint_t<Bits> &Value)
        {
            Bigint_t<Bits> Result{};

            for (uint8_t i = 0; i < Value.size(); ++i)
                Result[i] = ~Value[i];

            return Result;
        }
        friend constexpr Bigint_t<Bits> operator<<(const Bigint_t<Bits> &Value, size_t Shift)
        {
            Bigint_t<Bits> Result{};
            if (Shift >= 64)
            {
                for (size_t i = Value.size() - 1; i >= Shift / 64; --i)
                {
                    Result[i] = Value[i - Shift / 64];
                }
            }
            else
            {
                for (size_t i = Value.size() - 1; i > 0; --i)
                {
                    Result[i] = (Value[i] << Shift) | (Value[i - 1] >> (64 - Shift));
                }
                Result[0] = Value[0] << Shift;
            }
            return Result;
        }
        friend constexpr Bigint_t<Bits> operator>>(const Bigint_t<Bits> &Value, size_t Shift)
        {
            Bigint_t<Bits> Result{};
            if (Shift >= 64)
            {
                for (size_t i = 0; i < Value.size() - Shift / 64; ++i)
                {
                    Result[i] = Value[i + Shift / 64];
                }
            }
            else
            {
                for (size_t i = 0; i < Value.size() - 1; ++i)
                {
                    Result[i] = (Value[i] >> Shift) | (Value[i + 1] << (64 - Shift));
                }
                Result[Value.size() - 1] = Value[Value.size() - 1] >> Shift;
            }
            return Result;
        }

        friend constexpr Bigint_t<Bits> operator|(const Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right)
        {
            Bigint_t<Bits> Result{};

            for (uint8_t i = 0; i < Left.size(); ++i)
                Result[i] = Left[i] | Right[i];

            return Result;
        }
        friend constexpr Bigint_t<Bits> operator&(const Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right)
        {
            Bigint_t<Bits> Result{};

            for (uint8_t i = 0; i < Left.size(); ++i)
                Result[i] = Left[i] & Right[i];

            return Result;
        }
        friend constexpr Bigint_t<Bits> operator^(const Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right)
        {
            Bigint_t<Bits> Result{};

            for (uint8_t i = 0; i < Left.size(); ++i)
                Result[i] = Left[i] ^ Right[i];

            return Result;
        }

        friend constexpr Bigint_t<Bits> operator|(const Bigint_t<Bits> &Left, const uint64_t &Right)
        {
            Bigint_t<Bits> Result = Left;
            Result[0] |= Right;
            return Result;
        }
        friend constexpr Bigint_t<Bits> operator&(const Bigint_t<Bits> &Left, const uint64_t &Right)
        {
            Bigint_t<Bits> Result = Left;
            Result[0] &= Right;
            return Result;
        }
        friend constexpr Bigint_t<Bits> operator^(const Bigint_t<Bits> &Left, const uint64_t &Right)
        {
            Bigint_t<Bits> Result = Left;
            Result[0] ^= Right;
            return Result;
        }

        // Compound operators, should be optimized out.
        friend constexpr Bigint_t<Bits> &operator<<=(Bigint_t<Bits> &Value, size_t Shift) { Value = Value << Shift; return Value; }
        friend constexpr Bigint_t<Bits> &operator>>=(Bigint_t<Bits> &Value, size_t Shift) { Value = Value >> Shift; return Value; }
        friend constexpr Bigint_t<Bits> &operator|=(Bigint_t<Bits> &Left, const uint64_t &Right) { Left = Left | Right; return Left; }
        friend constexpr Bigint_t<Bits> &operator&=(Bigint_t<Bits> &Left, const uint64_t &Right) { Left = Left & Right; return Left; }
        friend constexpr Bigint_t<Bits> &operator^=(Bigint_t<Bits> &Left, const uint64_t &Right) { Left = Left ^ Right; return Left; }
        friend constexpr Bigint_t<Bits> &operator|=(Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right) { Left = Left | Right; return Left; }
        friend constexpr Bigint_t<Bits> &operator&=(Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right) { Left = Left & Right; return Left; }
        friend constexpr Bigint_t<Bits> &operator^=(Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right) { Left = Left ^ Right; return Left; }
    };

    using uint128_t = Bigint_t<128>;
    using uint256_t = Bigint_t<256>;
    using uint512_t = Bigint_t<512>;
}

// Sepearated big-integer arithmetic in-case we want to use a native uint128_t in the future.
namespace qDSA::Bigint
{
    using namespace Detail;

    // Need some constant-time comparators.
    constexpr bool LT(const uint64_t &X, const uint64_t &Y)
    {
        return (X ^ ((X ^ Y) | ((X - Y) ^ Y))) >> 63;
    }
    constexpr bool isZero(const uint64_t &Value)
    {
        return 1 ^ ((Value | (0 - Value)) >> 63);
    }

    // Could be replaced with intrinsics / assembly later. TODO(tcn): Benchmark.
    constexpr std::pair<uint64_t, bool> ADDC(const uint64_t &X, const uint64_t &Y, bool Carry = false)
    {
        const auto Temp = X + Carry;
        const auto Sum = Temp + Y;
        const auto Overflow = LT(Temp, Carry) | LT(Sum, Temp);

        return { Sum, Overflow };
    }
    constexpr std::pair<uint64_t, bool> SUBC(const uint64_t &X, const uint64_t &Y, bool Borrow = false)
    {
        const auto Temp = X - Y;
        const auto Underflow = LT(X, Y) | (Borrow & isZero(Temp));
        const auto Diff = Temp - Borrow;

        return { Diff, Underflow };
    }

    // Heavy enough that we should extract and optimize.
    constexpr uint128_t Product(const uint64_t &X, const uint64_t &Y)
    {
        // Constexpr fallback.
        auto Product_fallback = [](const uint64_t &X, const uint64_t &Y) -> Bigint_t<128>
        {
            const uint64_t Lowmask = 0xFFFFFFFFULL;
            const uint32_t XLow = X & Lowmask, XHigh = X >> 32;
            const uint32_t YLow = Y & Lowmask, YHigh = Y >> 32;

            const auto B00 = uint64_t(XLow) * YLow;
            const auto B01 = uint64_t(XLow) * YHigh;
            const auto B10 = uint64_t(XHigh) * YLow;
            const auto B11 = uint64_t(XHigh) * YHigh;

            const auto M1 = B10 + (B00 >> 32);
            const auto M2 = B01 + (M1 & Lowmask);

            // Maybe need an ADDC?
            const uint64_t High = B11 + (M1 >> 32) + (M2 >> 32);
            const uint64_t Low = ((M2 & Lowmask) << 32) | (B00 & Lowmask);

            return { Low, High };
        };

        if (std::is_constant_evaluated())
            return Product_fallback(X, Y);

        // x64
        #if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
        uint64_t Low{}, High{};
        asm(
            "mulq %3\n\t"
            : "=a"(Low), "=d"(High)
            : "%0"(X), "rm"(Y));
        return { Low, High };

        // AArch64
        #elif (defined(__GNUC__) || defined(__clang__)) && defined(__aarch64__)
        uint64_t Low{}, High{};
        asm(
            "mul %0, %2, %3\n\t"
            "umulh %1, %2, %3\n\t"
            : "=&r"(Low), "=r"(High)
            : "r"(X), "r"(Y));
        return { Low, High };

        // x64
        #elif defined(_MSC_VER) && defined(_M_X64)
        uint64_t Low{}, High{};
        Low = _umul128(X, Y, &High);
        return { Low, High };

        // AArch64
        #elif defined(_MSC_VER) && defined(_M_ARM64)
        return { (X * Y), __umulh(X, Y) };

        // 32-bit architectures.
        #else
        return Product_fallback(X, Y);
        #endif
    }

    // Optimized for common case of small inputs.
    template <size_t Bits> constexpr std::pair<Bigint_t<Bits>, bool> ADDC(const Bigint_t<Bits> &Left, const uint64_t &Right, uint8_t Offset = 0)
    {
        Bigint_t<Bits> Result{ Left };
        bool Carry = false;

        std::tie(Result[Offset], Carry) = ADDC(Result[Offset], Right, Carry);

        for (size_t i = Offset + 1; i < Bigint_t<Bits>::size(); ++i)
        {
            std::tie(Result[i], Carry) = ADDC(Result[i], 0ULL, Carry);
        }

        return { Result, Carry };
    }
    template <size_t Bits> constexpr std::pair<Bigint_t<Bits>, bool> SUBC(const Bigint_t<Bits> &Left, const uint64_t &Right, uint8_t Offset = 0)
    {
        Bigint_t<Bits> Result{ Left };
        bool Borrow = false;

        std::tie(Result[Offset], Borrow) = SUBC(Result[Offset], Right, Borrow);

        for (size_t i = Offset + 1; i < Bigint_t<Bits>::size(); ++i)
        {
            std::tie(Result[i], Borrow) = SUBC(Result[i], 0ULL, Borrow);
        }

        return { Result, Borrow };
    }
    template <size_t Bits> constexpr Bigint_t<Bits * 2> MUL(const Bigint_t<Bits> &Left, const uint64_t &Right)
    {
        Bigint_t<Bits * 2> Result{};
        uint64_t Temp = 0;

        for (size_t i = 0; i < Bigint_t<Bits>::size(); ++i)
        {
            const auto [Low, High] = Product(Left[i], Right).asPair();
            const auto [Lowsum, Lowcarry] = ADDC(Low, Temp);
            const auto [Highsum, Highcarry] = ADDC(Result[i], Lowsum);

            Temp = High + Lowcarry + Highcarry;
            Result[i] = Highsum;
        }

        Result[Bigint_t<Bits>::size()] = Temp;

        return Result;
    }

    // Generics for large integers, no need for an input flag.
    template <size_t LBits, size_t RBits> constexpr std::pair<Bigint_t<LBits>, bool> ADDC(const Bigint_t<LBits> &Left, const Bigint_t<RBits> &Right, uint8_t Offset = 0)
    {
        static_assert(LBits >= RBits, "Left operand must be larger or equal in size to the right operand.");

        Bigint_t<LBits> Result{ Left };
        bool Carry = false;

        for (size_t i = 0; i < Bigint_t<RBits>::size(); ++i)
        {
            std::tie(Result[Offset + i], Carry) = ADDC(Result[Offset + i], Right[i], Carry);
        }

        for (size_t i = Offset + Bigint_t<RBits>::size(); i < Bigint_t<LBits>::size(); ++i)
        {
            std::tie(Result[i], Carry) = ADDC(Result[i], 0ULL, Carry);
        }

        return { Result, Carry };
    }
    template <size_t LBits, size_t RBits> constexpr std::pair<Bigint_t<LBits>, bool> SUBC(const Bigint_t<LBits> &Left, const Bigint_t<RBits> &Right, uint8_t Offset = 0)
    {
        static_assert(LBits >= RBits, "Left operand must be larger or equal in size to the right operand.");

        Bigint_t<LBits> Result{ Left };
        bool Borrow = false;

        for (size_t i = 0; i < Bigint_t<RBits>::size(); ++i)
        {
            std::tie(Result[Offset + i], Borrow) = SUBC(Result[Offset + i], Right[i], Borrow);
        }

        for (size_t i = Offset + Bigint_t<RBits>::size(); i < Bigint_t<LBits>::size(); ++i)
        {
            std::tie(Result[i], Borrow) = SUBC(Result[i], 0ULL, Borrow);
        }

        return { Result, Borrow };
    }
    template <size_t Bits> constexpr Bigint_t<Bits * 2> MUL(const Bigint_t<Bits> &Left, const Bigint_t<Bits> &Right)
    {
        Bigint_t<Bits * 2> Result{};

        for (size_t i = 0; i < Bigint_t<Bits>::size(); ++i)
        {
            uint64_t Temp = 0;

            for (size_t j = 0; j < Bigint_t<Bits>::size(); ++j)
            {
                const auto [Low, High] = Product(Left[i], Right[j]).asPair();
                const auto [Lowsum, Lowcarry] = ADDC(Low, Temp);
                const auto [Highsum, Highcarry] = ADDC(Result[i + j], Lowsum);

                Temp = High + Lowcarry + Highcarry;
                Result[i + j] = Highsum;
            }

            Result[i + Bigint_t<Bits>::size()] = Temp;
        }

        return Result;
    }

    // Some arithmetic on Mersenne primes can be simplified by bitflips.
    template <size_t Bits> constexpr bool getMSB(const Bigint_t<Bits> &Value) { return (Value[Bigint_t<Bits>::size() - 1] >> 63) & 1; }
    template <size_t Bits> constexpr bool getLSB(const Bigint_t<Bits> &Value) { return Value[0] & 1; }
    template <size_t Bits> constexpr Bigint_t<Bits> setMSB(const Bigint_t<Bits> &Value, bool MSB)
    {
        auto Copy = Value;
        Copy[Bigint_t<Bits>::size() - 1] &= 0x7fffffffffffffffULL;
        Copy[Bigint_t<Bits>::size() - 1] |= uint64_t(MSB) << 63;

        return Copy;
    }
}

// Modular arithmetic over the Mersenne prime (2^127 - 1).
namespace qDSA::Fieldelements
{
    using namespace Detail;

    /*
        NOTE(tcn):
        This code is designed for the compiler to reason about.
        Eliding copies etc. rather than modifying references.
        This is because of constexpr limitations in MSVC.
    */

    // FE1271_t represents a 128-bit unsigned integer, operations are done in cryptographically constant time.
    struct FE1271_t : uint128_t
    {
        using uint128_t::asBytes;

        // Standard initialization.
        constexpr FE1271_t() : uint128_t() {}
        constexpr FE1271_t(FE1271_t &&) = default;
        constexpr FE1271_t(const FE1271_t &) = default;
        constexpr FE1271_t(uint64_t Low) : uint128_t(Low) {}
        constexpr FE1271_t(uint128_t Value) : uint128_t(Value) {}
        constexpr FE1271_t(uint64_t Low, uint64_t High) : uint128_t({ Low, High }) {}

        // Implicitly deleted..
        constexpr FE1271_t &operator=(FE1271_t &&) = default;
        constexpr FE1271_t &operator=(const FE1271_t &) = default;

        // Partial-reduction mod P (2^127 - 1)
        constexpr FE1271_t Reduce_add() const
        {
            // ADDC(*this, _bittestandreset64((*this)[1], 63));
            const uint64_t MSB = Bigint::getMSB(*this);
            const auto Clear = Bigint::setMSB(*this, 0);
            return Bigint::ADDC(Clear, MSB).first;
        }
        constexpr FE1271_t Reduce_sub() const
        {
            // SUBC(*this, _bittestandreset64((*this)[1], 63));
            const uint64_t MSB = Bigint::getMSB(*this);
            const auto Clear = Bigint::setMSB(*this, 0);
            return Bigint::SUBC(Clear, MSB).first;
        }
        constexpr FE1271_t Reduce_full() const
        {
            return Negate().Negate();
        }

        // X^(-1) == X^(P-2) % P
        constexpr FE1271_t Invert() const
        {
            const auto Square = [](FE1271_t Value, uint8_t N)
            {
                for (uint8_t i = 0; i < N; ++i)
                    Value *= Value;

                return Value;
            };

            const auto X = *this;

            // 69 MUL
            const auto A = X * Square(X, 1);
            const auto B = A * Square(A, 2);
            const auto C = B * Square(B, 4);
            const auto D = C * Square(C, 8);
            const auto E = D * Square(D, 16);
            const auto F = E * Square(E, 32);

            // 66 MUL
            const auto G = E * Square(F, 32);
            const auto H = D * Square(G, 16);
            const auto I = C * Square(H, 8);
            const auto J = B * Square(I, 4);
            const auto K = X * Square(J, 3);

            // 3 MUL
            return K * Square(X, 2);
        }

        // X^((P+1)/4) == X^(2^125)
        constexpr FE1271_t SQRT() const
        {
            const auto X = *this;
            auto Acc = X * X;

            for (uint8_t i = 1; i < 125; ++i)
                Acc *= Acc;

            return Acc;
        }

        // Combined SQRT checking.
        constexpr std::pair<FE1271_t, bool> trySQRT() const
        {
            const auto X = *this;
            const auto Root = X.SQRT().Reduce_add();
            const auto Valid = (Root * Root) == X;

            return { Root, Valid };
        }

        // Need to check against both zero and the prime.
        constexpr bool isZero() const
        {
            const FE1271_t Prime = { uint64_t(-1), uint64_t(-1) >> 1 };
            const FE1271_t Zero{};

            const bool isPrime = ((*this)[0] == Prime[0]) & ((*this)[1] == Prime[1]);
            const bool isZero = ((*this)[0] == Zero[0]) & ((*this)[1] == Zero[1]);

            return isPrime | isZero;
        }

        // Flip the sign.
        constexpr FE1271_t Negate() const
        {
            return FE1271_t{} - *this;
        }

        // Assumes partially-reduced input.
        friend constexpr FE1271_t operator+(const FE1271_t &Left, const FE1271_t &Right)
        {
            return FE1271_t{ Bigint::ADDC(Left, Right).first }.Reduce_add();
        }
        friend constexpr FE1271_t operator-(const FE1271_t &Left, const FE1271_t &Right)
        {
            return FE1271_t{ Bigint::SUBC(Left, Right).first }.Reduce_sub();
        }
        friend constexpr FE1271_t operator*(const FE1271_t &Left, const FE1271_t &Right)
        {
            const auto [Low, High] = cmp::splitArray<2>(Bigint::MUL(Left, Right));
            const uint128_t Overflow = (uint128_t(High) << 1) | Bigint::getMSB(uint128_t(Low));

            return FE1271_t{ Bigint::setMSB(uint128_t(Low), 0) } + FE1271_t{ Overflow };
        }

        friend constexpr FE1271_t operator+(const FE1271_t &Left, const uint64_t &Right)
        {
            return FE1271_t{ Bigint::ADDC(Left, Right).first }.Reduce_add();
        }
        friend constexpr FE1271_t operator-(const FE1271_t &Left, const uint64_t &Right)
        {
            return FE1271_t{ Bigint::SUBC(Left, Right).first }.Reduce_sub();
        }
        friend constexpr FE1271_t operator*(const FE1271_t &Left, const uint64_t &Right)
        {
            const auto [Low, High] = cmp::splitArray<2>(Bigint::MUL(Left, Right));
            const uint128_t Overflow = (uint128_t(High) << 1) | Bigint::getMSB(uint128_t(Low));

            return FE1271_t{ Bigint::setMSB(uint128_t(Low), 0) } + FE1271_t{ Overflow };
        }

        // Compound operators, should be optimized out.
        friend constexpr FE1271_t &operator<<=(FE1271_t &Value, uint8_t Shift) { Value = Value << Shift; return Value; }
        friend constexpr FE1271_t &operator>>=(FE1271_t &Value, uint8_t Shift) { Value = Value >> Shift; return Value; }
        friend constexpr FE1271_t &operator+=(FE1271_t &Left, const FE1271_t &Right) { Left = Left + Right; return Left; }
        friend constexpr FE1271_t &operator-=(FE1271_t &Left, const FE1271_t &Right) { Left = Left - Right; return Left; }
        friend constexpr FE1271_t &operator*=(FE1271_t &Left, const FE1271_t &Right) { Left = Left * Right; return Left; }
        friend constexpr FE1271_t &operator+=(FE1271_t &Left, const uint64_t &Right) { Left = Left + Right; return Left; }
        friend constexpr FE1271_t &operator-=(FE1271_t &Left, const uint64_t &Right) { Left = Left - Right; return Left; }
        friend constexpr FE1271_t &operator*=(FE1271_t &Left, const uint64_t &Right) { Left = Left * Right; return Left; }
        friend constexpr FE1271_t &operator|=(FE1271_t &Left, const uint64_t &Right) { Left = Left | Right; return Left; }
        friend constexpr FE1271_t &operator&=(FE1271_t &Left, const uint64_t &Right) { Left = Left & Right; return Left; }
    };

    using Compressedpoint_t = std::array<FE1271_t, 2>;
    using Kummerpoint_t = std::array<FE1271_t, 4>;
}

// Scalars are reduced to 250-bit rather than 256-bit as the top will always be 0.
namespace qDSA::Scalar
{
    using namespace Bigint;

    // Scalar reduction around N, 250-bit result.
    constexpr uint256_t Reduce(const uint512_t &Value)
    {
        constexpr uint256_t L0 = { 0x47730B4B840C05BDULL, 0xD2C27FC9F9A154FFULL, 0x0334D69820C75294ULL };
        constexpr uint256_t L6 = { 0xDCC2D2E103016F40ULL, 0xB09FF27E68553FD1ULL, 0xCD35A60831D4A534ULL };

        uint512_t Buffer{ Value };

        for (uint8_t i = 0; i < 4; ++i)
        {
            const auto [Blo, Bhi] = cmp::splitArray<4>(Buffer);
            const auto [Plo, Phi] = cmp::splitArray<4>(MUL(uint256_t(Bhi), L6));

            Buffer = ADDC(uint512_t(Blo + Phi), uint256_t(Plo)).first;
        }

        // Adjust.
        Buffer[4] = (Buffer[4] << 6) | ((Buffer[3] & 0xFC00000000000000ULL) >> 58);
        Buffer[3] &= 0x03FFFFFFFFFFFFFFULL;

        for (uint8_t i = 0; i < 1; ++i)
        {
            const auto [Blo, Bhi] = cmp::splitArray<4>(Buffer);
            const auto [Plo, Phi] = cmp::splitArray<4>(MUL(uint256_t(Bhi), L0));

            Buffer = ADDC(uint512_t(Blo + Phi), uint256_t(Plo)).first;
        }

        // Adjust.
        Buffer[4] = (Buffer[3] & 0x0400000000000000ULL) >> 58;
        Buffer[3] &= 0x03FFFFFFFFFFFFFFULL;

        for (uint8_t i = 0; i < 1; ++i)
        {
            const auto [Blo, Bhi] = cmp::splitArray<4>(Buffer);
            const auto [Plo, Phi] = cmp::splitArray<4>(MUL(uint256_t(Bhi), L0));

            Buffer = ADDC(uint512_t(Blo + uint256_t()), uint256_t(Plo)).first;
        }

        return uint256_t{ Buffer[0], Buffer[1], Buffer[2], Buffer[3] };
    }

    // Swap the sign by rotating around N.
    constexpr uint256_t Negate(const uint256_t &Value)
    {
        constexpr uint256_t N = { 0xB88CF4B47BF3FA43ULL, 0x2D3D8036065EAB00ULL, 0xFCCB2967DF38AD6BULL, 0x03FFFFFFFFFFFFFFULL };
        return SUBC(N, Value).first;
    }

    // s = (r - hd') % N.
    constexpr uint256_t Scalarops(const uint256_t &R, const uint256_t &H, const uint256_t &D)
    {
        const auto Temp = MUL(H, D);
        const auto Red = Reduce(Temp);
        const auto Neg = Negate(Red);

        const uint512_t A = Neg + uint256_t{};
        const auto Large = ADDC(A, R).first;

        return Reduce(Large);
    }

    // Put input in scalar range.
    template <size_t N> constexpr uint256_t getScalar(const std::array<uint8_t, N> &Input) requires (N <= 64)
    {
        std::array<uint8_t, 64> Value{};
        std::ranges::copy(Input, Value.data());

        return Reduce(std::bit_cast<uint512_t>(Value));
    }
}

// We are using unsigned arithmetic, so the sign of the operations need to be flipped when used.
namespace qDSA::Constants
{
    using namespace Fieldelements;

    constexpr std::array<uint16_t, 4> Epsilon_hat = { /*(-)*/ 833, 2499, 1617, 561 };
    constexpr std::array<uint16_t, 4> Epsilon = { 114, /*(-)*/ 57, /*(-)*/ 66, /*(-)*/ 418 };
    constexpr std::array<uint16_t, 4> Kappa = { /*(-)*/ 4697, 5951, 5753, /*(-)*/ 1991 };
    constexpr std::array<uint16_t, 4> Kappa_hat = { /*(-)*/ 961, 128, 569, 1097 };
    constexpr std::array<uint16_t, 4> Mu = { /*(-)*/ 11, 22, 19, 3 };
    constexpr std::array<uint16_t, 4> Mu_hat = { /*(-)*/ 33, 11, 17, 49 };

    constexpr std::array<uint16_t, 8> CurveQ = { 3575, 9625, 4625, 12259, 11275, 7475, 6009, 43991 };
    constexpr FE1271_t CurveC = { 0x46F7E3D8CDDDA843ULL, 0x40F50EEFA320A2DDULL };
}

// The actual implementation of qDSA.
namespace qDSA::Algorithm
{
    using namespace Constants;

    // Constant-time swapping.
    constexpr void CSWAP(FE1271_t &A, FE1271_t &B, bool doSwap)
    {
        const auto Mask = 0ULL - doSwap;
        const auto Low = (A[0] ^ B[0]) & Mask;
        const auto High = (A[1] ^ B[1]) & Mask;

        A[0] ^= Low; B[0] ^= Low;
        A[1] ^= High; B[1] ^= High;
    }
    constexpr void CSWAP(Kummerpoint_t &A, Kummerpoint_t &B, bool doSwap)
    {
        CSWAP(A[0], B[0], doSwap);
        CSWAP(A[1], B[1], doSwap);
        CSWAP(A[2], B[2], doSwap);
        CSWAP(A[3], B[3], doSwap);
    }

    // 4-way operations on the elements.
    constexpr Kummerpoint_t MUL4(const Kummerpoint_t &Left, const Kummerpoint_t &Right)
    {
        return { (Left[0] * Right[0]), (Left[1] * Right[1]), (Left[2] * Right[2]), (Left[3] * Right[3]) };
    }
    constexpr Kummerpoint_t SQR4(const Kummerpoint_t &Value)
    {
        return MUL4(Value, Value);
    }

    // Hadamard transform for projections.
    constexpr Kummerpoint_t Hadamard(const Kummerpoint_t &Value)
    {
        const auto A = Value[1] - Value[0];
        const auto B = Value[2] + Value[3];
        const auto C = Value[0] + Value[1];
        const auto D = Value[2] - Value[3];

        return { A + B, A - B, D - C, C + D };

        return
        {
            Value[0] + Value[1] + Value[2] + Value[3],
            Value[0] + Value[1] - Value[2] - Value[3],
            Value[0] - Value[1] + Value[2] - Value[3],
            Value[0] - Value[1] - Value[2] + Value[3]
        };
    }
    constexpr Kummerpoint_t negHadamard(const Kummerpoint_t &Value)
    {
        const auto Tmp = Hadamard({ Value[0].Negate(), Value[1], Value[2], Value[3] });
        return { Tmp[0], Tmp[1], Tmp[2], Tmp[3].Negate() };
    }

    // Dot-product and a complement to handle negative inputs.
    constexpr FE1271_t Dot(const Kummerpoint_t &Left, const Kummerpoint_t &Right)
    {
        return { (Left[0] * Right[0]) + (Left[1] * Right[1]) + (Left[2] * Right[2]) + (Left[3] * Right[3]) };
    }
    constexpr FE1271_t negDot(const Kummerpoint_t &Left, const Kummerpoint_t &Right)
    {
        return { (Left[0] * Right[0]) - (Left[1] * Right[1]) - (Left[2] * Right[2]) + (Left[3] * Right[3]) };
    }

    // Evaluate the polynomials.
    constexpr FE1271_t evalK2(const FE1271_t &L1, const FE1271_t &L2, bool Tau)
    {
        // K2(L1, L2, Tau) = (Q5 * L1)^2 + (Q3 * L2)^2 + (Q4 * Tau)^2 - 2 * Q3 * (Q2 * L1 * L2 + Tau * (Q0 * L1 - Q1 * L2))

        const auto A = (CurveQ[5] * L1) * (CurveQ[5] * L1);     // (Q5 * L1)^2
        const auto B = (CurveQ[3] * L2) * (CurveQ[3] * L2);     // (Q3 * L2)^2
        const auto C = uint64_t(CurveQ[4]) * CurveQ[4] * Tau;   // (Q4 * Tau)^2
        const auto D = (CurveQ[0] * L1) - (CurveQ[1] * L2);     // Q0 * L1 - Q1 * L2
        const auto E = CurveQ[2] * L1 * L2;                     // Q2 * L1 * L2
        const auto F = CurveQ[3] * (E + Tau * D);               // Q3 * (Q2 * L1 * L2 + Tau * D)

        return A + B + C - (F + F);

    }
    constexpr FE1271_t evalK3(const FE1271_t &L1, const FE1271_t &L2, bool Tau)
    {
        // K3(L1, L2, Tau) = Q3(Q0 * (L1^2 + Tau) * L2 - Q1 * L1  * (L2^2 + Tau) + Q2 * (L1^2 + L2^2) * Tau) - Q6 * Q7 * L1 * L2 * Tau

        const auto A = uint64_t(CurveQ[6]) * CurveQ[7] * L1 * L2 * Tau; // Q6 * Q7 * L1 * L2 * Tau
        const auto B = CurveQ[2] * (L1 * L1 + L2 * L2) * Tau;           // Q2 * (L1^2 + L2^2) * Tau
        const auto C = CurveQ[1] * L1 * (L2 * L2 + Tau);                // Q1 * L1  * (L2^2 + Tau)
        const auto D = CurveQ[0] * L2 * (L1 * L1 + Tau);                // Q0 * (L1^2 + Tau) * L2

        return uint64_t(CurveQ[3]) * (D - C + B) - A;
    }
    constexpr FE1271_t evalK4(const FE1271_t &L1, const FE1271_t &L2, bool Tau)
    {
        // K4(L1, L2, Tau) = ((Q3 * L1)^2 + (Q5 * L2)^2 - 2 * Q3 * L1 * L2 * (Q0 * L2 - Q1 * L1 + Q2)) * Tau + (Q4 * L1 * L2)^2

        const auto A = (CurveQ[3] * L1) * (CurveQ[3] * L1);             // (Q3 * L1)^2
        const auto B = (CurveQ[5] * L2) * (CurveQ[5] * L2);             // (Q5 * L2)^2
        const auto C = (CurveQ[4] * L1 * L2) * (CurveQ[4] * L1 * L2);   // (Q4 * L1 * L2)^2
        const auto D = (CurveQ[3] * L1 * L2) + (CurveQ[3] * L1 * L2);   // 2 * Q3 * L1 * L2
        const auto E = (CurveQ[0] * L2) - (CurveQ[1] * L1) + CurveQ[2]; // Q0 * L2 - Q1 * L1 + Q2

        return (A + B - D * E) * Tau + C;
    }

    // Precompute inverted Kummer-point coordinates.
    constexpr Kummerpoint_t Wrap(const Kummerpoint_t &Value)
    {
        const auto A = Value[1] * Value[2];
        const auto B = Value[0] * (A * Value[3]).Invert();
        const auto C = B * Value[3];

        return { FE1271_t{}, C * Value[2], C * Value[1], A * B };
    }
    constexpr Kummerpoint_t Unwrap(const Kummerpoint_t &Value)
    {
        const auto A = Value[2] * Value[3];
        const auto B = Value[1] * Value[3];
        const auto C = Value[1] * Value[2];

        return { C * Value[3], A, B, C };
    }

    // Combined pseudo-addition and doubling on K^2.
    constexpr std::tuple<Kummerpoint_t, Kummerpoint_t> DBLADD(const Kummerpoint_t &P, const Kummerpoint_t &Q, const Kummerpoint_t &Diff)
    {
        auto Xq = Hadamard(Q);
        auto Xp = Hadamard(P);

        Xq = MUL4(Xq, Xp);
        Xp = SQR4(Xp);

        Xp = MUL4(Xp, { Epsilon_hat[0], Epsilon_hat[1], Epsilon_hat[2], Epsilon_hat[3] });
        Xq = MUL4(Xq, { Epsilon_hat[0], Epsilon_hat[1], Epsilon_hat[2], Epsilon_hat[3] });

        Xp = SQR4(Hadamard(Xp));
        Xq = SQR4(Hadamard(Xq));

        Xp = MUL4(Xp, { Epsilon[0], Epsilon[1], Epsilon[2], Epsilon[3] });
        Xq = MUL4(Xq, { 1, Diff[1], Diff[2], Diff[3] });

        return { Xp, Xq };
    }

    // Montgomery ladder over the X coordinate.
    constexpr Kummerpoint_t Ladder(Kummerpoint_t Q, const Kummerpoint_t &Wrapped, const std::array<uint8_t, 32> &Scalar)
    {
        auto P = Kummerpoint_t{ Mu[0], Mu[1], Mu[2], Mu[3] };
        uint8_t Previous = false;

        // While the algorithm calls for 256 bits, we can save 6 iterations.
        for (int i = 250; i >= 0; --i)
        {
            const auto Bit = (Scalar[i >> 3] >> (i & 0x07)) & 1;
            const auto Swap = Bit ^ Previous;
            Previous = Bit;

            // Negate X.
            Q[0] = Q[0].Negate();
            CSWAP(Q, P, Swap);

            std::tie(P, Q) = DBLADD(P, Q, Wrapped);
        }

        // Negate X.
        P[0] = P[0].Negate();
        CSWAP(Q, P, Previous);

        return { P };
    }
    constexpr Kummerpoint_t Ladder(const std::array<uint8_t, 32> &Scalar)
    {
        constexpr Kummerpoint_t Basepoint =
        {
            FE1271_t{},
            { 0xaeb351a64e931a48ULL, 0x1be0c3dc2049c2e7ULL },
            { 0x64659818e07e36dfULL, 0x23b416cd8eaba630ULL },
            { 0xc7ae3d057215441eULL, 0x5db35c384447a24dULL }
        };

        return Ladder(Unwrap(Basepoint), Basepoint, Scalar);
    }

    // Evaluate one of the off-diagonal B_{ij}
    constexpr Kummerpoint_t BII(const Kummerpoint_t &sP, const Kummerpoint_t &hQ)
    {
        auto P = SQR4(sP);
        auto Q = SQR4(hQ);

        P = MUL4(P, { Epsilon_hat[0], Epsilon_hat[1], Epsilon_hat[2], Epsilon_hat[3] });
        Q = MUL4(Q, { Epsilon_hat[0], Epsilon_hat[1], Epsilon_hat[2], Epsilon_hat[3] });

        // Negate as hat[0] should be negative.
        P[0] = P[0].Negate();
        Q[0] = Q[0].Negate();

        const auto U = Kummerpoint_t
        {
            Dot({P[0],P[1],P[2],P[3]}, { Q[0], Q[1], Q[2], Q[3] }),
            Dot({P[0],P[1],P[2],P[3]}, { Q[1], Q[0], Q[3], Q[2] }),
            Dot({P[0],P[2],P[1],P[3]}, { Q[2], Q[0], Q[3], Q[1] }),
            Dot({P[0],P[3],P[1],P[2]}, { Q[3], Q[0], Q[2], Q[1] })
        };
        Q = Kummerpoint_t
        {
            negDot({ U[0], U[1], U[2], U[3] }, { Kappa[0], Kappa[1], Kappa[2], Kappa[3] }),
            negDot({ U[1], U[0], U[3], U[2] }, { Kappa[0], Kappa[1], Kappa[2], Kappa[3] }),
            negDot({ U[2], U[3], U[0], U[1] }, { Kappa[0], Kappa[1], Kappa[2], Kappa[3] }),
            negDot({ U[3], U[2], U[1], U[0] }, { Kappa[0], Kappa[1], Kappa[2], Kappa[3] })
        };

        Q = MUL4(Q, { Mu_hat[0], Mu_hat[1], Mu_hat[2], Mu_hat[3] });

        // Negate as mu[0] should be negative.
        Q[0] = Q[0].Negate();

        return Q;
    }
    constexpr FE1271_t BIJ(const Kummerpoint_t &P, const Kummerpoint_t &Q, const Kummerpoint_t &Constants)
    {
        const auto A = P[0] * P[1];
        const auto B = P[2] * P[3];
        const auto C = Q[0] * Q[1];
        const auto D = Q[2] * Q[3];

        const auto X = (A - B) * (C - D) * Constants[2] * Constants[3];
        const auto Y = B * D * (Constants[2] * Constants[3] + Constants[0] * Constants[1]);
        const auto Z = (Y - X) * Constants[0] * Constants[1] * (Constants[1] * Constants[3] + Constants[0] * Constants[2]);

        return Z * (Constants[1] * Constants[2] + Constants[0] * Constants[3]);
    }

    // Dot by k-hat and mu-hat to handle the sign switch.
    constexpr FE1271_t kRow(const Kummerpoint_t &Value)
    {
        return
            (Value[1] * Kappa_hat[1]) +
            (Value[2] * Kappa_hat[2]) +
            (Value[3] * Kappa_hat[3]) -
            (Value[0] * Kappa_hat[0]);
    }
    constexpr FE1271_t mRow(const Kummerpoint_t &Value)
    {
        return
            ((Value[1] + Value[1] - Value[0]) * Mu[0]) +
            (Value[2] * Mu[2]) +
            (Value[3] * Mu[3]);
    }

    // Mapping between K^2 and K^3.
    constexpr Compressedpoint_t Compress(const Kummerpoint_t &Input)
    {
        const auto &[X, Y, Z, W] = Input;
        const auto L1 = kRow({ W, Z, Y, X });
        const auto L2 = kRow({ Z, W, X, Y });
        const auto L3 = kRow({ Y, X, W, Z });
        const auto L4 = kRow({ X, Y, Z, W });

        // Tau = L3 != 0
        const auto [Tau, Lambda] = [&]() -> std::pair<bool, FE1271_t>
        {
            if (!L3.isZero()) return { true,  L3.Invert() };
            if (!L2.isZero()) return { false, L2.Invert() };
            if (!L1.isZero()) return { false, L1.Invert() };

            return { false, L4.Invert() };
        }();

        // Normalize.
        const auto l1 = L1 * Lambda;
        const auto l2 = L2 * Lambda;
        const auto l4 = L4 * Lambda;

        // Evaluate the polynomial.
        const auto K2 = evalK2(l1, l2, Tau);
        const auto K3 = evalK3(l1, l2, Tau);

        const auto R = K2 * l4 - K3;
        const auto Sigma = Bigint::getLSB(R);

        // Save the signs in L1 and L2's unused bits.
        return { Bigint::setMSB(l1, Tau), Bigint::setMSB(l2, Sigma) };
    }
    constexpr std::optional<Kummerpoint_t> Decompress(const Compressedpoint_t &Input)
    {
        Kummerpoint_t L{};

        // We store the sign of compressed values in the top bit.
        const auto Tau = Bigint::getMSB(Input[0]), Sigma = Bigint::getMSB(Input[1]);
        const FE1271_t L1 = Bigint::setMSB(Input[0], 0), L2 = Bigint::setMSB(Input[1], 0);

        // Evaluate the polynomials.
        const auto K2 = evalK2(L1, L2, Tau);
        const auto K3 = evalK3(L1, L2, Tau);
        const auto K4 = evalK4(L1, L2, Tau);

        if (K2.isZero())
        {
            if (K3.isZero())
            {
                // Invalid compression.
                if (!L1.isZero() | !L2.isZero() | Tau | Sigma)
                    return std::nullopt;

                // Identity.
                L = { 0, 0, 0, 1 };
            }
            else if (Sigma != Bigint::getLSB(K3.Negate()))
            {
                // K4 = 2 * K3 * L4
                L = { (L1 * K3) + (L1 * K3), (L2 * K3) + (L2 * K3), (Tau * K3) + (Tau * K3), K4 };
            }
            else
            {
                // Invalid compression.
                return std::nullopt;
            }
        }
        else
        {
            const auto Delta = (K3 * K3) - (K2 * K4);
            auto [R, Valid] = Delta.trySQRT();

            // TODO TODO TEST REMOVE SIGN

            // Select the right root.
            if (Bigint::getLSB(R) ^ Sigma)
                R = R.Negate();

            // No preimage in K^3
            if (!Valid) return std::nullopt;

            // K2 * L4 = K3 + R
            L = { K2 * L1, K2 * L2, K2 * Tau, K3 + R };
        }

        // Remap from K^3 to K^2
        return Kummerpoint_t
        {
            mRow({L[3], L[2], L[1], L[0]}),
            mRow({L[2], L[3], L[0], L[1]}),
            mRow({L[1], L[0], L[3], L[2]}),
            mRow({L[0], L[1], L[2], L[3]})
        };
    }

    // Verify the quadratic relationship.
    constexpr bool isQuad(const FE1271_t &Bij, const FE1271_t &Bjj, const FE1271_t &Bii, const FE1271_t &R1, const FE1271_t &R2)
    {
        // BjjR1^2 - 2*C*BijR1R2 + BiiR2^2 == 0

        const auto A = Bjj * R1 * R1;
        const auto B = CurveC * Bij * R1 * R2;
        const auto C = Bii * R2 * R2;

        return (A - B - B + C).isZero();
    }

    // If R exists in { P + Q, P - Q }
    constexpr bool Check(const Kummerpoint_t &sP, const Kummerpoint_t &hQ, const Kummerpoint_t &r)
    {
        const auto P = negHadamard(sP), Q = negHadamard(hQ), R = negHadamard(r);
        const auto Bii = BII(P, Q);

        // B_{1,2}
        auto Bij = BIJ(P, Q, { Mu_hat[0], Mu_hat[1], Mu_hat[2], Mu_hat[3] });
        auto Invalid = !isQuad(Bij, Bii[1], Bii[0], R[0], R[1]);

        // B_{1,3}
        Bij = BIJ({ P[0], P[2], P[1], P[3] }, { Q[0], Q[2], Q[1], Q[3] }, { Mu_hat[0], Mu_hat[2], Mu_hat[1], Mu_hat[3] });
        Invalid |= !isQuad(Bij, Bii[2], Bii[0], R[0], R[2]);

        // B_{1,4}
        Bij = BIJ({ P[0], P[3], P[1], P[2] }, { Q[0], Q[3], Q[1], Q[2] }, { Mu_hat[0], Mu_hat[3], Mu_hat[1], Mu_hat[2] });
        Invalid |= !isQuad(Bij, Bii[3], Bii[0], R[0], R[3]);

        // B_{2,3}
        Bij = FE1271_t{} - BIJ({ P[1], P[2], P[0], P[3] }, { Q[1], Q[2], Q[0], Q[3] }, { Mu_hat[1], Mu_hat[2], Mu_hat[0], Mu_hat[3] });
        Invalid |= !isQuad(Bij, Bii[2], Bii[1], R[1], R[2]);

        // B_{2,4}
        Bij = FE1271_t{} - BIJ({ P[1], P[3], P[0], P[2] }, { Q[1], Q[3], Q[0], Q[2] }, { Mu_hat[1], Mu_hat[3], Mu_hat[0], Mu_hat[2] });
        Invalid |= !isQuad(Bij, Bii[3], Bii[1], R[1], R[3]);

        // B_{3,4}
        Bij = FE1271_t{} - BIJ({ P[2], P[3], P[0], P[1] }, { Q[2], Q[3], Q[0], Q[1] }, { Mu_hat[2], Mu_hat[3], Mu_hat[0], Mu_hat[1] });
        Invalid |= !isQuad(Bij, Bii[3], Bii[2], R[2], R[3]);

        return !Invalid;
    }
}

// API implementation.
namespace qDSA
{
    using namespace Algorithm;
    using namespace Scalar;

    // Create a keypair from a random seed.
    constexpr Publickey_t getPublickey(const Privatekey_t &Privatekey)
    {
        // PK = [d']P
        const auto Scalar = getScalar(Privatekey);
        const auto dP = Ladder(Scalar.asBytes());
        const auto Q = Compress(dP);

        return std::bit_cast<Publickey_t>(Q);
    }
    template <typename T> constexpr std::pair<Publickey_t, Privatekey_t> Createkeypair(const T &Seed)
    {
        auto Privatekey = Hash::SHA256(Seed);

        // Make the private-key compatible with RFC 8032 in-case someone want's to re-use it.
        Privatekey[0] &= uint8_t(0xF8);
        Privatekey[31] &= uint8_t(0x7F);
        Privatekey[31] |= uint8_t(0x40);

        return { getPublickey(Privatekey), Privatekey };
    }

    // Compute a shared secret between two keypairs (A.PK, B.SK) == (B.PK, A.SK)
    constexpr std::optional<Sharedkey_t> Generatesecret(const Publickey_t &Publickey, const Privatekey_t &Privatekey)
    {
        const auto PK = Decompress(std::bit_cast<Compressedpoint_t>(Publickey));
        if (!PK) return std::nullopt;

        const auto Scalar = getScalar(Privatekey);
        const auto Secret = Ladder(*PK, Wrap(*PK), Scalar.asBytes());
        return std::bit_cast<Sharedkey_t>(Compress(Secret));
    }

    // Create a signature for the provided message, somewhat hardened against hackery.
    template <cmp::Sequential_t C> constexpr Signature_t Sign(const Publickey_t &Publickey, const Privatekey_t &Privatekey, const C &Message)
    {
        const auto Buffer = cmp::getBytes(Message);

        // r = H(d'' || M)
        const auto r = [&]()
        {
            // 32 bytes of deterministic 'randomness'.
            Blob_t Local; Local.reserve(Buffer.size() + 32);
            Local.append(Hash::SHA512(Privatekey), 8, 32);
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();

        // Compressed [r]P
        const auto rP = Compress(Ladder(r.asBytes()));

        // h = H(R || Q || M)
        const auto h = [&]()
        {
            Blob_t Local; Local.reserve(16 + 16 + 32 + Buffer.size());
            Local.append(rP[0].asBytes().data(), 16);
            Local.append(rP[1].asBytes().data(), 16);
            Local.append(Publickey.data(), 32);
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();

        // Set the scalar positive.
        const auto H = Bigint::getLSB(h) ? Scalar::Negate(h) : h;

        // s = (r - hd') % N.
        const auto Scalar = Scalarops(r, H, getScalar(Privatekey));

        // Signature = [r]P || s
        return uint512_t{ std::bit_cast<uint256_t>(rP) + Scalar }.asBytes();
    }

    // Verify that the message was signed by the owner of the public key.
    template <cmp::Sequential_t C> constexpr bool Verify(const Publickey_t &Publickey, const Signature_t &Signature, const C &Message)
    {
        const auto Buffer = cmp::getBytes(Message);
        const auto [rP, Scalar] = cmp::splitArray<32>(Signature);
        const auto R = Decompress(std::bit_cast<Compressedpoint_t>(rP));
        const auto dP = Decompress(std::bit_cast<Compressedpoint_t>(Publickey));

        // Invalid public-key or signature.
        if (!dP || !R) return false;

        // H(R || Q || M)
        const auto h = [&]()
        {
            Blob_t Local; Local.reserve(32 + 32 + Buffer.size());
            Local.append(Signature.data(), 32);
            Local.append(Publickey.data(), 32);
            Local.append(Buffer.data(), Buffer.size());

            return getScalar(Hash::SHA512(Local));
        }();

        const auto sP = Ladder(getScalar(Scalar).asBytes());
        const auto hQ = Ladder(*dP, Wrap(*dP), h.asBytes());

        return Check(sP, hQ, *R);
    }
}
