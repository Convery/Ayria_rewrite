/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-31
    License: MIT

    OpenSSL provided utilities.
*/

#pragma once
#include <Utilities/Utilities.hpp>

#if defined (HAS_OPENSSL)
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace Hash
{
    template <cmp::Byte_t T> std::array<uint8_t, 16> MD5(const T *Input, const size_t Size)
    {
        const auto Context = EVP_MD_CTX_create();
        auto Buffer = std::array<uint8_t, 16>{};

        EVP_DigestInit_ex(Context, EVP_md5(), nullptr);
        EVP_DigestUpdate(Context, Input, Size);
        EVP_DigestFinal_ex(Context, Buffer.data(), nullptr);
        EVP_MD_CTX_destroy(Context);

        return std::bit_cast<std::array<uint8_t, 16>>(Buffer);
    }
    template <cmp::Byte_t T> std::array<uint8_t, 20> SHA1(const T *Input, const size_t Size)
    {
        const auto Context = EVP_MD_CTX_create();
        auto Buffer = std::array<uint8_t, 20>{};

        EVP_DigestInit_ex(Context, EVP_sha1(), nullptr);
        EVP_DigestUpdate(Context, Input, Size);
        EVP_DigestFinal_ex(Context, Buffer.data(), nullptr);
        EVP_MD_CTX_destroy(Context);

        return std::bit_cast<std::array<uint8_t, 20>>(Buffer);
    }

    inline std::array<uint8_t, 20> HMACSHA1(const void *Input, const size_t Size, const void *Key, const size_t Keysize)
    {
        unsigned int Buffersize = 20;
        unsigned char Buffer[20]{};

        HMAC(EVP_sha1(), Key, (int)Keysize, (const uint8_t *)Input, (int)Size, Buffer, &Buffersize);
        return std::bit_cast<std::array<uint8_t, 20>>(Buffer);
    }
    inline std::array<uint8_t, 32> HMACSHA256(const void *Input, const size_t Size, const void *Key, const size_t Keysize)
    {
        unsigned int Buffersize = 32;
        unsigned char Buffer[32]{};

        HMAC(EVP_sha256(), Key, (int)Keysize, (const uint8_t *)Input, (int)Size, Buffer, &Buffersize);
        return std::bit_cast<std::array<uint8_t, 32>>(Buffer);
    }
    inline std::array<uint8_t, 64> HMACSHA512(const void *Input, const size_t Size, const void *Key, const size_t Keysize)
    {
        unsigned int Buffersize = 64;
        unsigned char Buffer[64]{};

        HMAC(EVP_sha512(), Key, (int)Keysize, (const uint8_t *)Input, (int)Size, Buffer, &Buffersize);
        return std::bit_cast<std::array<uint8_t, 64>>(Buffer);
    }

    // Any range.
    template <cmp::Range_t T> constexpr cmp::Array_t<uint8_t, 16> MD5(const T &Input)
    {
        const auto Span = cmp::getBytes(Input);
        return MD5(Span.data(), Span.size());
    }
    template <cmp::Range_t T> constexpr cmp::Array_t<uint8_t, 20> SHA1(const T &Input)
    {
        const auto Span = cmp::getBytes(Input);
        return SHA1(Span.data(), Span.size());
    }

    // Any other typed value.
    template <typename T> requires (!cmp::Range_t<T>) constexpr cmp::Array_t<uint8_t, 16> MD5(const T &Input)
    {
        return MD5(cmp::getBytes(Input));
    }
    template <typename T> requires (!cmp::Range_t<T>) constexpr cmp::Array_t<uint8_t, 20> SHA1(const T &Input)
    {
        return SHA1(cmp::getBytes(Input));
    }

    // String literals.
    template <cmp::Char_t T, size_t N> constexpr cmp::Array_t<uint8_t, 16> MD5(const T(&Input)[N])
    {
        return MD5(cmp::getBytes(cmp::stripNullchar(Input)));
    }
    template <cmp::Char_t T, size_t N> constexpr cmp::Array_t<uint8_t, 20> SHA1(const T(&Input)[N])
    {
        return SHA1(cmp::getBytes(cmp::stripNullchar(Input)));
    }
}

// CBC mode, don't reuse IVs.
namespace AES
{
    template <cmp::Byte_t T, size_t N> std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>
    Encrypt_128(std::span<T, N> Plaintext, std::span<T, 16> Cryptokey, std::span<T, 16> Initialvector)
    {
        std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))> Buffer;
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        EVP_EncryptInit(Context, EVP_aes_128_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.data(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.data() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return std::bit_cast<std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>>(Buffer);
    }
    template <cmp::Byte_t T, size_t N> std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>
    Encrypt_192(std::span<T, N> Plaintext, std::span<T, 24> Cryptokey, std::span<T, 24> Initialvector)
    {
        std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))> Buffer;
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        EVP_EncryptInit(Context, EVP_aes_192_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.data(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.data() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return std::bit_cast<std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>>(Buffer);
    }
    template <cmp::Byte_t T, size_t N> std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>
    Encrypt_256(std::span<T, N> Plaintext, std::span<T, 32> Cryptokey, std::span<T, 32> Initialvector)
    {
        std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))> Buffer;
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        EVP_EncryptInit(Context, EVP_aes_256_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.data(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.data() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return std::bit_cast<std::array<uint8_t, (N & 15) == 0 ? N : (N + (16 - (N & 15)))>>(Buffer);
    }

    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Encrypt_128(std::span<T> Plaintext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Plaintext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        assert(Initialvector.size() >= (128 / 8));
        assert(Cryptokey.size() >= (128 / 8));

        EVP_EncryptInit(Context, EVP_aes_128_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.get(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.get() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Encryptionlength };
    }
    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Encrypt_192(std::span<T> Plaintext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Plaintext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        assert(Initialvector.size() >= (192 / 8));
        assert(Cryptokey.size() >= (192 / 8));

        EVP_EncryptInit(Context, EVP_aes_192_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.get(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.get() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Encryptionlength };
    }
    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Encrypt_256(std::span<T> Plaintext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Plaintext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        assert(Initialvector.size() >= (256 / 8));
        assert(Cryptokey.size() >= (256 / 8));

        EVP_EncryptInit(Context, EVP_aes_256_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.get(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.get() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Encryptionlength };
    }

    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Decrypt_128(std::span<T> Ciphertext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Ciphertext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Decryptionlength = 0;

        assert(Initialvector.size() >= (128 / 8));
        assert(Cryptokey.size() >= (128 / 8));

        EVP_DecryptInit(Context, EVP_aes_128_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_DecryptUpdate(Context, Buffer.get(), &Decryptionlength, (const uint8_t *)Ciphertext.data(), int(Ciphertext.size()));
        EVP_DecryptFinal_ex(Context, Buffer.get() + Decryptionlength, &Decryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Decryptionlength };
    }
    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Decrypt_192(std::span<T> Ciphertext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Ciphertext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Decryptionlength = 0;

        assert(Initialvector.size() >= (192 / 8));
        assert(Cryptokey.size() >= (192 / 8));

        EVP_DecryptInit(Context, EVP_aes_192_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_DecryptUpdate(Context, Buffer.get(), &Decryptionlength, (const uint8_t *)Ciphertext.data(), int(Ciphertext.size()));
        EVP_DecryptFinal_ex(Context, Buffer.get() + Decryptionlength, &Decryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Decryptionlength };
    }
    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Decrypt_256(std::span<T> Ciphertext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Ciphertext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Decryptionlength = 0;

        assert(Initialvector.size() >= (256 / 8));
        assert(Cryptokey.size() >= (256 / 8));

        EVP_DecryptInit(Context, EVP_aes_256_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_DecryptUpdate(Context, Buffer.get(), &Decryptionlength, (const uint8_t *)Ciphertext.data(), int(Ciphertext.size()));
        EVP_DecryptFinal_ex(Context, Buffer.get() + Decryptionlength, &Decryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Decryptionlength };
    }
}

namespace DES3
{
    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Encrypt(std::span<T> Plaintext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Plaintext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Encryptionlength = 0;

        assert(Initialvector.size() >= (192 / 8));
        assert(Cryptokey.size() >= (192 / 8));

        EVP_EncryptInit(Context, EVP_des_ede3_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_EncryptUpdate(Context, Buffer.get(), &Encryptionlength, (const uint8_t *)Plaintext.data(), int(Plaintext.size()));
        EVP_EncryptFinal_ex(Context, Buffer.get() + Encryptionlength, &Encryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Encryptionlength };
    }

    template <cmp::Byte_t T, cmp::Byte_t U, cmp::Byte_t V> cmp::Vector_t<T>
    Decrypt(std::span<T> Ciphertext, std::span<U> Cryptokey, std::span<V> Initialvector)
    {
        const auto N = Ciphertext.size();
        const auto Buffer = std::make_unique<uint8_t[]>((N & 15) == 0 ? N : (N + (16 - (N & 15))));
        EVP_CIPHER_CTX *Context = EVP_CIPHER_CTX_new();
        int Decryptionlength = 0;

        assert(Initialvector.size() >= (192 / 8));
        assert(Cryptokey.size() >= (192 / 8));

        EVP_DecryptInit(Context, EVP_des_ede3_cbc(), (const uint8_t *)Cryptokey.data(), (const uint8_t *)Initialvector.data());
        EVP_DecryptUpdate(Context, Buffer.get(), &Decryptionlength, (const uint8_t *)Ciphertext.data(), int(Ciphertext.size()));
        EVP_DecryptFinal_ex(Context, Buffer.get() + Decryptionlength, &Decryptionlength);
        EVP_CIPHER_CTX_free(Context);

        return { Buffer.get(), Decryptionlength };
    }
}

#endif
