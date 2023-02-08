/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-17
    License: MIT

    A simple container prefixing data with a byte-ID.
    Data is written as little endian.
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include "Utilities/Encoding/UTF8.hpp"
#include "Utilities/Strings/toHexstring.hpp"
#include "Utilities/Strings/Variadicstring.hpp"

// Unreachable code warning.
#pragma warning(push)
#pragma warning(disable : 4702)

// 32 bytes on x64, 16 on x86.
struct Bytebuffer_t
{
    // Type identifiers for the stream.
    enum Datatypes_t : uint8_t
    {
        BB_NONE = 0,        // Padding / NOP.

        BB_BLOB,            // uint8_t
        BB_ASCIISTRING,     // char
        BB_UTF8STRING,      // char8_t
        BB_UNICODESTRING,   // wchar_t

        // POD.
        BB_BOOL,
        BB_SINT8,
        BB_UINT8,
        BB_SINT16,
        BB_UINT16,
        BB_SINT32,
        BB_UINT32,
        BB_SINT64,
        BB_UINT64,
        BB_FLOAT32,
        BB_FLOAT64,

        BB_ARRAY = 100, // Arraytypes, e.g. 100 + BB_UINT16 = vector<uint16_t>

        BB_MAX
    };

    // Default to owning mode if nothing else is specified..
    std::variant<const uint8_t *, uint8_t *> Internalbuffer{ (uint8_t *)nullptr };
    size_t Internaliterator{};
    size_t Internalsize{};

    // Cleanup for owning mode.
    ~Bytebuffer_t()
    {
        if (isOwning())
        {
            if (const auto Buffer = std::get<uint8_t *>(Internalbuffer))
            {
                free(Buffer);
            }
        }
    }

    // Utility access.
    void Rewind() noexcept
    {
        Internaliterator = 0;
    }

    [[nodiscard]] uint8_t Peek() const noexcept
    {
        const auto pByte = data(true);
        if (!pByte) return BB_NONE;
        return uint8_t(*pByte);
    }
    [[nodiscard]] bool isOwning() const noexcept
    {
        return std::holds_alternative<uint8_t *>(Internalbuffer);
    }
    void Seek(int Offset, int Origin) noexcept
    {
        switch (Origin)
        {
            case SEEK_SET:
                Internaliterator = std::clamp(size_t(Offset), size_t(0), Internalsize);
                break;

            case SEEK_CUR:
                Internaliterator = std::clamp(size_t(Internaliterator + Offset), size_t(0), Internalsize);
                break;

            // A negative offset is undefined in C, so just take the absolute value.
            case SEEK_END:
                Internaliterator = std::clamp(size_t(Internalsize - std::abs(Offset)), size_t(0), Internalsize);
                break;

            default:
                // NOP
                assert(false);
        }
    }
    void Expandbuffer(size_t Extracapacity)
    {
        // Need to switch to owning mode.
        if (!isOwning())
        {
            const auto Originalbuffer = std::get<const uint8_t *>(Internalbuffer);
            const auto Originalsize = Internalsize;

            // Original buffer should never be nullptr, but better safe than sorry..
            const auto Newbuffer = (uint8_t *)malloc(Originalsize + Extracapacity);
            if (Originalbuffer) std::memcpy(Newbuffer, Originalbuffer, Originalsize);
            std::memset(Newbuffer + Originalsize, 0, Extracapacity);

            Internalbuffer = Newbuffer;
            Internalsize = Originalsize + Extracapacity;
        }

        // Else we just alloc/realloc.
        else
        {
            const auto Originalbuffer = std::get<uint8_t *>(Internalbuffer);
            const auto Originalsize = Internalsize;

            if (Originalbuffer)
            {
                const auto Newbuffer = (uint8_t *)realloc(Originalbuffer, Originalsize + Extracapacity);
                std::memset(Newbuffer + Originalsize, 0, Extracapacity);

                Internalbuffer = Newbuffer;
                Internalsize = Originalsize + Extracapacity;
            }
            else
            {
                const auto Newbuffer = (uint8_t *)malloc(Originalsize + Extracapacity);
                std::memset(Newbuffer + Originalsize, 0, Extracapacity);

                Internalbuffer = Newbuffer;
                Internalsize = Originalsize + Extracapacity;
            }
        }
    }
    [[nodiscard]] size_t size(bool Remainder = false) const noexcept
    {
        return Internalsize - (Remainder ? Internaliterator : 0);
    }
    [[nodiscard]] const uint8_t *data(bool atOffset = false) const noexcept
    {
        // Trying to get a pointer past our allocation.
        if (atOffset && (Internaliterator >= Internalsize)) return nullptr;

        // Assume that the iterator is at a valid position.
        if (isOwning()) return std::get<uint8_t *>(Internalbuffer) + (atOffset ? Internaliterator : 0);
        else return std::get<const uint8_t *>(Internalbuffer) + (atOffset ? Internaliterator : 0);
    }

    // Construct a non-owning buffer from common containers.
    Bytebuffer_t(const void *Buffer, size_t Size)
    {
        Internalbuffer = (const uint8_t *)Buffer;
        Internaliterator = 0;
        Internalsize = Size;
    }
    template <cmp::Range_t Range> Bytebuffer_t(const Range &Input)
    {
        const auto Validated = cmp::getBytes(Input);
        Internalbuffer = Validated.data();
        Internalsize = Validated.size();
        Internaliterator = 0;
    }

    // Construct a owning buffer.
    Bytebuffer_t() = default;
    explicit Bytebuffer_t(size_t Size)
    {
        const auto Newbuffer = (uint8_t *)malloc(Size);
        std::memset(Newbuffer, 0, Size);

        Internalbuffer = Newbuffer;
        Internaliterator = 0;
        Internalsize = Size;
    }

    // Copying a buffer takes a read-only view of it, prefer moving.
    explicit Bytebuffer_t(const Bytebuffer_t &Other) noexcept
    {
        Internalbuffer = Other.data();
        Internalsize = Other.Internalsize;
        Internaliterator = Other.Internaliterator;
    }
    Bytebuffer_t(Bytebuffer_t &&Other) noexcept
    {
        Internalbuffer.swap(Other.Internalbuffer);
        Internaliterator = Other.Internaliterator;
        Internalsize = Other.Internalsize;
    }

    // Direct IO on the buffer.
    bool rawRead(size_t Size, void *Buffer = nullptr) noexcept
    {
        // If there's not enough data left we can't do much.
        if ((Internaliterator + Size) > Internalsize) [[unlikely]]
            return false;

        // If there's a buffer, just memcpy into it.
        if (Buffer) std::memcpy(Buffer, data(true), Size);

        Internaliterator += Size;
        return true;
    }
    void rawWrite(size_t Size, const void *Buffer = nullptr)
    {
        // If we were created as non-owning we need to do a realloc regardless.
        if (!isOwning() || ((Internaliterator + Size) > Internalsize))
            Expandbuffer((Internaliterator + Size) - Internalsize);

        // A null-buffer is just a memset.
        const auto pBuffer = std::get<uint8_t *>(Internalbuffer) + Internaliterator;
        if (Buffer) std::memcpy(pBuffer, Buffer, Size);
        else std::memset(pBuffer, 0, Size);

        Internaliterator += Size;
    }

    // Deduce the ID from the datatype.
    template <typename Type> static constexpr uint8_t toID()
    {
        // If it's an enumeration, get the base type.
        if constexpr (std::is_enum_v<Type>) return toID<std::underlying_type_t<Type>>();

        // Vectors are either arrays or blobs of data.
        if constexpr (cmp::isDerived<Type, std::vector> || requires { cmp::isDerived<Type, std::array>; } || requires { cmp::isDerived<Type, std::span>; })
        {
            // Do not use spans for strings, they are a headache to deal with.
            if constexpr (requires { cmp::isDerived<Type, std::span>; })
            {
                if constexpr (cmp::isDerived<Type, std::span> && std::is_same_v<std::decay_t<typename Type::value_type>, char>) assert(false);
                if constexpr (cmp::isDerived<Type, std::span> && std::is_same_v<std::decay_t<typename Type::value_type>, char8_t>) assert(false);
                if constexpr (cmp::isDerived<Type, std::span> && std::is_same_v<std::decay_t<typename Type::value_type>, wchar_t>) assert(false);
            }

            if constexpr (std::is_same_v<std::decay_t<typename Type::value_type>, uint8_t>) return BB_BLOB;
            return BB_ARRAY + toID<std::decay_t<typename Type::value_type>>();
        }

        // Strings of std::span is not supported because they are a pain.
        if constexpr (cmp::isDerived<Type, std::basic_string> || cmp::isDerived<Type, std::basic_string_view>)
        {
            if constexpr (std::is_same_v<std::decay_t<typename Type::value_type>, uint8_t>) return BB_BLOB;
            if constexpr (std::is_same_v<std::decay_t<typename Type::value_type>, char>) return BB_ASCIISTRING;
            if constexpr (std::is_same_v<std::decay_t<typename Type::value_type>, char8_t>) return BB_UTF8STRING;
            if constexpr (std::is_same_v<std::decay_t<typename Type::value_type>, wchar_t>) return BB_UNICODESTRING;
        }

        // POD.
        if constexpr (std::is_same_v<std::decay_t<Type>, bool>)             return BB_BOOL;
        if constexpr (std::is_same_v<std::decay_t<Type>, int8_t>)           return BB_SINT8;
        if constexpr (std::is_same_v<std::decay_t<Type>, uint8_t>)          return BB_UINT8;
        if constexpr (std::is_same_v<std::decay_t<Type>, int16_t>)          return BB_SINT16;
        if constexpr (std::is_same_v<std::decay_t<Type>, uint16_t>)         return BB_UINT16;
        if constexpr (std::is_same_v<std::decay_t<Type>, int32_t>)          return BB_SINT32;
        if constexpr (std::is_same_v<std::decay_t<Type>, uint32_t>)         return BB_UINT32;
        if constexpr (std::is_same_v<std::decay_t<Type>, int64_t>)          return BB_SINT64;
        if constexpr (std::is_same_v<std::decay_t<Type>, uint64_t>)         return BB_UINT64;
        if constexpr (std::is_same_v<std::decay_t<Type>, float>)            return BB_FLOAT32;
        if constexpr (std::is_same_v<std::decay_t<Type>, double>)           return BB_FLOAT64;

        // Extensions for POD where the user uses an aliased type.
        if constexpr (std::is_same_v<std::decay_t<Type>, char>)             return BB_SINT8;
        if constexpr (std::is_same_v<std::decay_t<Type>, char8_t>)          return BB_UINT8;
        if constexpr (std::is_same_v<std::decay_t<Type>, char16_t>)         return BB_UINT16;
        if constexpr (std::is_same_v<std::decay_t<Type>, wchar_t>)
        {
            // *nix and Windows disagree on how large a wchar_t is..
            if constexpr (sizeof(wchar_t) == 4)                             return BB_UINT32;
            else                                                            return BB_UINT16;
        }

        return BB_NONE;
    }

    // Typed IO, prefix the type with the ID.
    template <typename Type> bool Read(Type &Buffer, bool Typechecked = true)
    {
        constexpr auto ExpectedID = toID<Type>();
        const auto StoredID = Peek();

        // Verify that the requested type is compatible.
        if (Typechecked || ExpectedID > BB_ARRAY) [[likely]]
        {
            // Special case: padding / NAN / whatever.
            if (StoredID == BB_NONE) [[unlikely]]
            {
                Buffer = {};
                return rawRead(sizeof(StoredID));
            }

            // If the ID is not the same, do not modify.
            if (StoredID != ExpectedID) [[unlikely]]
                return false;

            // Advance past the type prefix.
            rawRead(sizeof(StoredID));
        }

        // Array of values.
        if constexpr (cmp::isDerived<Type, std::vector>)
        {
            const auto Size = Read<uint32_t>();
            const auto Count = Read<uint32_t>(false);

            // Can't trust anyone these days..
            if (Size == 0) [[unlikely]] return false;
            if (Count == 0) [[unlikely]] return false;
            if ((Count * sizeof(typename Type::value_type)) != Size) [[unlikely]] return false;

            // STL implements vector<bool> as a bitset, so we can't really memcpy.
            if constexpr (std::is_same_v<typename Type::value_type, bool>)
            {
                Buffer.resize(Count);
                for (size_t i = 0; i < Count; ++i)
                    Buffer[i] = Read<bool>(false);

                // Due to vector<bool>'s implementation, we can't really do a proper check.
                return true;
            }
            else
            {
                Buffer.resize(Count);
                const auto Result = rawRead(Size, Buffer.data());

                // Ensure endian-ness.
                if constexpr (std::is_integral_v<Type> || std::is_floating_point_v<Type>)
                {
                    for (size_t i = 0; i < Count; ++i)
                        Buffer[i] = cmp::fromLittle(Buffer[i]);
                }

                return Result;
            }
        }

        // Strings and blobs.
        if constexpr (cmp::isDerived<Type, std::basic_string>)
        {
            if constexpr (std::is_same_v<Type, Blob_t>)
            {
                // Just a size and a block of bytes..
                const auto Size = Read<uint32_t>(Typechecked);
                Buffer.resize(Size);

                return rawRead(Size, Buffer.data());
            }
            else
            {
                // Strings are null-terminated, so just initialize from the pointer.
                Buffer = Type((typename Type::value_type *)data(true));
                return rawRead((Buffer.size() + 1) * sizeof(typename Type::value_type));
            }
        }

        // POD.
        const auto Result = rawRead(sizeof(Type), &Buffer);
        if constexpr (std::is_integral_v<Type> || std::is_floating_point_v<Type>)
        {
            Buffer = cmp::fromLittle(Buffer);
        }
        return Result;
    }
    template <typename Type> void Write(Type &Value, bool Typechecked = true)
    {
        // User-specified serialization.
        if constexpr (requires { Value.Serialize(*this); }) return Value.Serialize(*this);

        // Special case of merging bytebuffers.
        else if constexpr (std::is_same_v<Type, Bytebuffer_t>)
        {
            // In-case we get a non-owning buffer, the caller needs to update the iterator.
            assert(Value.Internaliterator);

            rawWrite(std::min(Value.Internaliterator, Value.Internalsize), Value.data());
            return;
        }

        // If we encounter an optional, process it instead.
        else if constexpr (cmp::isDerived<Type, std::optional>)
        {
            if (Value) return Write(*Value, Typechecked);
            else return WriteNULL();
        }
        else
        {
            static_assert(toID<Type>() != BB_NONE, "Invalid datatype.");
        }

        // A byte prefix identifier for the type.
        constexpr auto TypeID = toID<Type>();
        if (Typechecked || TypeID > BB_ARRAY) [[likely]]
            rawWrite(sizeof(TypeID), &TypeID);

        // Array of values.
        if constexpr (cmp::isDerived<Type, std::vector> || requires { cmp::isDerived<Type, std::array>; } || requires { cmp::isDerived<Type, std::span>; })
        {
            // Array layout, total size and element count.
            Write<uint32_t>(uint32_t(sizeof(typename Type::value_type) * Value.size()));
            Write<uint32_t>((uint32_t)Value.size(), false);

            // No need for a type ID with each element.
            for (const auto &Item : Value)
                Write(Item, false);

            return;
        }

        // Strings and blobs.
        if constexpr (cmp::isDerived<Type, std::basic_string>)
        {
            if constexpr (std::is_same_v<Type, Blob_t>)
            {
                Write<uint32_t>((uint32_t)Value.size(), Typechecked);
                rawWrite(Value.size(), Value.data());
            }
            else
            {
                // Write as null-terminated string.
                rawWrite((Value.size() + 1) * sizeof(typename Type::value_type), Value.c_str());
            }

            return;
        }

        // POD.
        if constexpr (std::is_integral_v<Type> || std::is_floating_point_v<Type>)
        {
            Value = cmp::toLittle(Value);
        }
        else rawWrite(sizeof(Type), &Value);
    }
    template <typename Type> Type Read(bool Typechecked = true)
    {
        Type Result{};

        // User-specified serialization.
        if constexpr (requires { Result.Deserialize(*this); }) return Result.Deserialize(*this);

        Read(Result, Typechecked);
        return Result;
    }
    void WriteNULL()
    {
        return rawWrite(1);
    }

    // Helper for reading and writing.
    template <typename Type> Bytebuffer_t &operator<<(Type Value)
    {
        Write(Value);
        return *this;
    }
    template <typename Type> void operator>>(Type &Buffer)
    {
        Read(Buffer);
    }

    // Easier access to the internal storage.
    [[nodiscard]] std::span<const uint8_t> as_span() const
    {
        return { data(), size() };
    }

    // Helper for serializing the contents.
    [[nodiscard]] std::string to_hex(bool fromOffset = false) const
    {
        const auto Size = size(fromOffset); const auto Data = data(fromOffset);

        return Encoding::toHexstringU(std::span(Data, Size), true);
    }
    [[nodiscard]] std::string to_string() const
    {
        // Take a copy so we don't have to mess with the iterator.
        Bytebuffer_t Reader(data(), size());
        std::stringstream Output;
        uint8_t TypeID;

        Output << "{\n";
        while ((TypeID = Reader.Peek()))
        {
            Output << "    ";

            // Reflection would be nice to have..
            #define POD(x, y, ...) case x: Output << #y << " = " << __VA_ARGS__ Reader.Read<y>() << '\n'; break
            #define String(x, y) case x: Output << #y << " = " << Encoding::toASCII(Reader.Read<y>()) << '\n'; break

            #define Array_POD(x, y, ...) case BB_ARRAY + x: Output << #y << "[] = "; \
            for (const auto &Item : Reader.Read<std::vector<y>>()) Output << __VA_ARGS__ Item << ", "; Output << '\n'; break

            #define Array_String(x, y) case BB_ARRAY + x: Output << #y << "[] = "; \
            for (const auto &Item : Reader.Read<std::vector<y>>()) Output << Encoding::toASCII(Item) << ", "; Output << '\n'; break

            switch (TypeID)
            {
                POD(BB_BOOL, bool);
                POD(BB_SINT8, int8_t, (int));
                POD(BB_UINT8, uint8_t, (unsigned));
                POD(BB_SINT16, int16_t);
                POD(BB_UINT16, uint16_t);
                POD(BB_SINT32, int32_t);
                POD(BB_UINT32, uint32_t);
                POD(BB_SINT64, int64_t);
                POD(BB_UINT64, uint64_t);
                POD(BB_FLOAT32, float);
                POD(BB_FLOAT64, double);

                Array_POD(BB_BOOL, bool);
                Array_POD(BB_SINT8, int8_t, (int));
                Array_POD(BB_UINT8, uint8_t, (unsigned));
                Array_POD(BB_SINT16, int16_t);
                Array_POD(BB_UINT16, uint16_t);
                Array_POD(BB_SINT32, int32_t);
                Array_POD(BB_UINT32, uint32_t);
                Array_POD(BB_SINT64, int64_t);
                Array_POD(BB_UINT64, uint64_t);
                Array_POD(BB_FLOAT32, float);
                Array_POD(BB_FLOAT64, double);

                String(BB_ASCIISTRING, std::string);
                String(BB_UTF8STRING, std::u8string);
                Array_String(BB_ASCIISTRING, std::string);
                Array_String(BB_UTF8STRING, std::u8string);

                // No sane dev should use an array of blobs..
                case BB_BLOB:
                {
                    Output << "Blob_t = { " << Encoding::toHexstringU(Reader.Read<Blob_t>(), true) << "}\n";
                    break;
                }

                default:
                    Output << ">>> Deserialization failed for typeID: " << TypeID << '\n';
                    goto LABEL_EXIT;
            }

            #undef POD
            #undef String
            #undef Array_POD
            #undef Array_String
        }

        LABEL_EXIT:
        Output << '}';
        return std::string(Output.str());
    }
};

// Might as well reuse some plumbing for basic protobuffer support.
struct Protobuffer_t : Bytebuffer_t
{
    enum class Wiretype_t : uint8_t { VARINT, I64, STRING /* LEN */, I32 = 5, INVALID = 255 };
    uint32_t CurrentID{}; Wiretype_t Currenttype{};

    // Inherit constructors.
    using Bytebuffer_t::Bytebuffer_t;

    // Copying a buffer takes a read-only view of it, prefer moving.
    explicit Protobuffer_t(const Protobuffer_t &Other) noexcept : Bytebuffer_t(Other)
    {
        Currenttype = Other.Currenttype;
        CurrentID = Other.CurrentID;
    }
    Protobuffer_t(Protobuffer_t &&Other) noexcept : Bytebuffer_t(Other)
    {
        Currenttype = Other.Currenttype;
        CurrentID = Other.CurrentID;
    }

    // Encode as little endian.
    template <typename T> requires (sizeof(T) == 8) void EncodeI64(T Input)
    {
        Bytebuffer_t::Write<T>(cmp::toLittle(Input), false);
    }
    template <typename T> requires (sizeof(T) == 4) void EncodeI32(T Input)
    {
        Bytebuffer_t::Write<T>(cmp::toLittle(Input), false);
    }
    template <std::integral T> void EncodeVARINT(T Input)
    {
        std::array<uint8_t, 10> Buffer{};
        uint8_t Size = 0;

        // Little endian needed.
        Input = cmp::toLittle(Input);

        while (Input && Size < 10)
        {
            // MSB = continuation.
            Buffer[Size] = (Input | 0x80) & 0xFF;
            Input >>= 7;
            ++Size;
        }

        // Clear MSB.
        Buffer[Size] &= 0x7F;

        // Need at least 1 byte.
        if (Size == 0) Size = 1;
        rawWrite(Size, Buffer.data());
    }
    void EncodeSTRING(const std::u8string Input)
    {
        EncodeVARINT(Input.size());
        rawWrite(Input.size(), Input.data());
    }

    // Decode to host endian.
    template <typename T> requires (sizeof(T) == 8) T DecodeI64()
    {
        const auto I64 = Bytebuffer_t::Read<T>(false);
        return cmp::fromLittle(I64);
    }
    template <typename T> requires (sizeof(T) == 4) T DecodeI32()
    {
        const auto I32 = Bytebuffer_t::Read<T>(false);
        return cmp::fromLittle(I32);
    }
    uint64_t DecodeVARINT()
    {
        uint64_t Value{};

        for (int i = 0; i < 64; i += 7)
        {
            const auto Byte = Bytebuffer_t::Read<uint8_t>(false);
            Value |= (Byte & 0x7F) << i;

            // No continuation bit.
            if (!(Value & 0x80))
                break;
        }

        return Value;
    }
    std::u8string DecodeSTRING()
    {
        const auto Length = DecodeVARINT();
        std::u8string Result(Length, 0);

        rawRead(Length, Result.data());
        return Result;
    }

    // Sometimes the protocol wants ZigZag encoding over 2's compliment.
    template <std::integral T> T toZigZag(T Input)
    {
        return (Input >> 1) ^ -(Input & 1);
    }
    template <std::integral T> T fromZigZag(T Input)
    {
        constexpr size_t Bits = sizeof(T) * 8 - 1;
        return (Input >> Bits) ^ (Input << 1);
    }

    // Tags are silly things.
    void EncodeTAG(uint32_t ID, Wiretype_t Type)
    {
        const uint64_t Tag = (ID << 3) | uint8_t(Type);
        EncodeVARINT(Tag);
    }
    std::pair<uint32_t, Wiretype_t> DecodeTAG()
    {
        const auto Tag = DecodeVARINT();

        // EOF
        if (Tag == 0) [[unlikely]]
        {
            Bytebuffer_t::Seek(0, SEEK_SET);
            return { 0, Wiretype_t::INVALID };
        }

        return { uint32_t(Tag >> 3), Wiretype_t(Tag & 7) };
    }

    // Seek tags.
    bool Seek(uint32_t ID)
    {
        // Early exit.
        if (ID == CurrentID && ID != 0)
            return true;

        // Seek from begining.
        if (ID < CurrentID)
        {
            CurrentID = 0;

            Bytebuffer_t::Seek(0, SEEK_SET);
            return Seek(ID);
        }

        // Seek forward.
        while(true)
        {
            std::tie(CurrentID, Currenttype) = DecodeTAG();

            if (Wiretype_t::INVALID == Currenttype) [[unlikely]] return false;
            if (ID == CurrentID) [[unlikely]] return true;

            // Skip the data.
            switch (Currenttype)
            {
                case Wiretype_t::VARINT: { (void)DecodeVARINT(); break; }
                case Wiretype_t::STRING: { (void)DecodeSTRING(); break; }
                case Wiretype_t::I64:    { (void)DecodeI64<uint64_t>(); break; }
                case Wiretype_t::I32:    { (void)DecodeI32<uint32_t>(); break; }
            }
        }
    }

    // Typed IO, need explicit type when writing, tries to convert when reading.
    template <typename T> void Write(T Value, Wiretype_t Type, uint32_t ID)
    {
        // User-specified serialization.
        if constexpr (requires { Value.Serialize(*this); }) return Value.Serialize(*this);

        // Special case of merging bytebuffers.
        if constexpr (std::is_same_v<T, Bytebuffer_t> || std::is_same_v<T, Protobuffer_t>)
        {
            // In-case we get a non-owning buffer, the caller needs to update the iterator.
            assert(Value.Internaliterator);

            rawWrite(std::min(Value.Internaliterator, Value.Internalsize), Value.data());
            return;
        }

        EncodeTAG(ID, Type);
        if (Type == Wiretype_t::VARINT) EncodeVARINT(Value);
        if (Type == Wiretype_t::STRING) EncodeSTRING(Value);
        if (Type == Wiretype_t::I64) EncodeI64(Value);
        if (Type == Wiretype_t::I32) EncodeI32(Value);
    }
    template <typename Type> bool Read(Type &Buffer, uint32_t ID)
    {
        // Lookup ID.
        if (!Seek(ID))
        {
            Errorprint(va("Protobuf tag %u not found", ID));
            return false;
        }

        if (Currenttype == Wiretype_t::VARINT)
        {
                 if constexpr (std::is_integral_v<Type>) Buffer = (Type)DecodeVARINT();
            else if constexpr (std::is_floating_point_v<Type>) Buffer = (Type)DecodeVARINT();
            else
            {
                Debugprint(va("Error: Protobuf tag %u type is VARINT", ID));
                return false;
            }

            return true;
        }

        if (Currenttype == Wiretype_t::I64)
        {
            if constexpr (sizeof(Type) == 8) Buffer = DecodeI64<Type>();
            else
            {
                Debugprint(va("Error: Protobuf tag %u type is I64", ID));
                return false;
            }

            return true;
        }

        if (Currenttype == Wiretype_t::I32)
        {
            if constexpr (sizeof(Type) == 4) Buffer = DecodeI32<Type>();
            else
            {
                Debugprint(va("Error: Protobuf tag %u type is I32", ID));
                return false;
            }

            return true;
        }

        if (Currenttype == Wiretype_t::STRING)
        {
            if constexpr (cmp::isDerived<Type, std::basic_string>)
            {
                const auto String = DecodeSTRING();
                if constexpr (std::is_same_v<Type, std::u8string>) Buffer = String;
                else if constexpr (std::is_same_v<Type, std::string>) Buffer = Encoding::toASCII(String);
                else if constexpr (std::is_same_v<Type, std::wstring>) Buffer = Encoding::toUNICODE(String);
                else if constexpr (std::is_same_v<Type, Blob_t>) Buffer = { String.begin(), String.end() };
                else
                {
                    Debugprint(va("Error: Protobuf tag %u type is LEN, but could not convert it?", ID));
                    return false;
                }

                return true;
            }
            else
            {
                Debugprint(va("Error: Protobuf tag %u type is LEN", ID));
                return false;
            }
        }

        // WTF?
        assert(false);
        return false;
    }
    template <typename Type> Type Read(uint32_t ID)
    {
        Type Result{};

        // User-specified serialization.
        if constexpr (requires { Result.Deserialize(*this); }) return Result.Deserialize(*this);

        Read(Result, ID);
        return Result;
    }
};

// Helper to serialize structs until we get reflection..
namespace Bytebuffer
{
    static decltype(auto) Members(const auto &Object, const auto &Visitor)
    {
        using Type = std::remove_cvref_t<decltype(Object)>;

             if constexpr (requires { [](Type &This) { auto &[a1] = This; }; }) { auto &[a1] = Object; return Visitor(a1); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2] = This; }; }) { auto &[a1, a2] = Object; return Visitor(a1, a2); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3] = This; }; }) { auto &[a1, a2, a3] = Object; return Visitor(a1, a2, a3); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4] = This; }; }) { auto &[a1, a2, a3, a4] = Object; return Visitor(a1, a2, a3, a4); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4, a5] = This; }; }) { auto &[a1, a2, a3, a4, a5] = Object; return Visitor(a1, a2, a3, a4, a5); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4, a5, a6] = This; }; }) { auto &[a1, a2, a3, a4, a5, a6] = Object; return Visitor(a1, a2, a3, a4, a5, a6); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4, a5, a6, a7] = This; }; }) { auto &[a1, a2, a3, a4, a5, a6, a7] = Object; return Visitor(a1, a2, a3, a4, a5, a6, a7); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4, a5, a6, a7, a8] = This; }; }) { auto &[a1, a2, a3, a4, a5, a6, a7, a8] = Object; return Visitor(a1, a2, a3, a4, a5, a6, a7, a8); }
        else if constexpr (requires { [](Type &This) { auto &[a1, a2, a3, a4, a5, a6, a7, a8, a9] = This; }; }) { auto &[a1, a2, a3, a4, a5, a6, a7, a8, a9] = Object; return Visitor(a1, a2, a3, a4, a5, a6, a7, a8, a9); }

        else
        {
            static_assert(std::is_void_v<decltype(Object)>, "Need a bigger boat..");
        }
    }

    static Bytebuffer_t fromStruct(const auto &Object)
    {
        Bytebuffer_t Buffer{};

        Members(Object, [&](auto && ...Items) -> void
        {
            ((Buffer << Items), ...);
        });

        return Buffer;
    }
}

#pragma warning (pop)
