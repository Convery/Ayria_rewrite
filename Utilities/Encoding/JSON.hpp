/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-07
    License: MIT
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include "Utilities/Strings/Variadicstring.hpp"

namespace JSON
{
    #pragma warning(push)
    #pragma warning(disable : 4702)

    enum class Basetype_t { Null, Bool, Float, Signedint, Unsignedint, String, Object, Array };
    using Object_t = std::map<std::u8string, struct Value_t>;
    using Array_t = std::vector<struct Value_t>;
    using String_t = std::u8string;

    // Get the enum type from typeinfo.
    template <typename T> constexpr Basetype_t getEnum()
    {
        // Ayria internal type takes priority over the underlying string type.
        if constexpr (std::is_same_v<T, Blob_view_t>) return Basetype_t::Array;
        if constexpr (std::is_same_v<T, Blob_t>) return Basetype_t::Array;

        if constexpr (cmp::isDerived<T, std::unordered_map>) return Basetype_t::Object;
        if constexpr (cmp::isDerived<T, std::map>) return Basetype_t::Object;

        if constexpr (cmp::isDerived<T, std::unordered_set>) return Basetype_t::Array;
        if constexpr (cmp::isDerived<T, std::vector>) return Basetype_t::Array;
        if constexpr (cmp::isDerived<T, std::set>) return Basetype_t::Array;

        if constexpr (cmp::isDerived<T, std::basic_string_view>) return Basetype_t::String;
        if constexpr (cmp::isDerived<T, std::basic_string>) return Basetype_t::String;

        if constexpr (std::is_floating_point_v<T>) return Basetype_t::Float;
        if constexpr (std::is_same_v<T, bool>) return Basetype_t::Bool;

        if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) return Basetype_t::Unsignedint;
        if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Basetype_t::Signedint;

        return Basetype_t::Null;
    }

    // Wrapper to hold all possible types.
    struct Value_t
    {
        using Null_t = std::monostate;
        using enum Basetype_t;

        // Use a variant and enum over std::shared_ptr<void> & enum for portability.
        std::variant<Null_t, bool, double, int64_t, uint64_t, String_t, Object_t, Array_t> Storage{ Null_t{} };

        // Getters and setters.
        template <typename T> requires(!std::is_pointer_v<T>) void Set(const T &Value)
        {
            // Optional support.
            if constexpr (cmp::isDerived<T, std::optional>)
            {
                if (Value) return Set(*Value);
                else return;
            }

            // Validate the type.
            const auto Type = getEnum<T>();
            if (Type == Null) return;

            // POD.
            if constexpr (Type == Unsignedint) Storage = Value;
            if constexpr (Type == Signedint) Storage = Value;
            if constexpr (Type == Float) Storage = Value;
            if constexpr (Type == Bool) Storage = Value;

            // Convert to the base type.
            if constexpr (Type == String) Storage = Encoding::toUTF8(Value);
            if constexpr (cmp::isDerived<T, std::unordered_map> || cmp::isDerived<T, std::map>)
            {
                // Already a base type, just copy.
                if constexpr (std::is_same_v<T, Object_t>)
                {
                    Storage = Value;
                }
                else
                {
                    static_assert(cmp::isDerived<typename T::key_type, std::basic_string>, "Keys for objects need to be strings.");

                    Object_t Object{};

                    for (const auto &[Key, _Value] : Value)
                        Object.emplace(Encoding::toUTF8(Key), _Value);
                    Storage = std::move(Object);
                }
            }
            if constexpr (cmp::isDerived<T, std::set> || cmp::isDerived<T, std::unordered_set> || cmp::isDerived<T, std::vector>)
            {
                static_assert(std::is_convertible_v<typename T::value_type, Value_t>, "Array elements must be convertible to Value_t.");

                // Already a base type, just copy.
                if constexpr (std::is_same_v<T, Array_t>)
                {
                    Storage = Value;
                }
                else
                {
                    Array_t Array(Value.size());

                    for (auto [Index, _Value] : Enumerate(Value))
                        Array[Index] = _Value;
                    Storage = std::move(Array);
                }
            }
        }
        template <typename T> requires(!std::is_pointer_v<T> && !cmp::isDerived<T, std::basic_string_view>) std::optional<T> Get() const
        {
            // We can't really return anything..
            if (std::holds_alternative<Null_t>(Storage)) return std::nullopt;

            constexpr auto Type = getEnum<T>();
            if constexpr (Type == Null)
            {
                // Improper use.
                assert(false);
                return {};
            }

            // POD.
            if constexpr (Type == Unsignedint) if (std::holds_alternative<uint64_t>(Storage)) return std::get<uint64_t>(Storage);
            if constexpr (Type == Signedint) if (std::holds_alternative<int64_t>(Storage)) return std::get<int64_t>(Storage);
            if constexpr (Type == Float) if (std::holds_alternative<double>(Storage)) return std::get<double>(Storage);
            if constexpr (Type == Bool) if (std::holds_alternative<bool>(Storage)) return std::get<bool>(Storage);

            // Convert to the expected encoding.
            if constexpr (Type == String) if (std::holds_alternative<String_t>(Storage))
            {
                if constexpr (std::is_same_v<typename T::value_type, wchar_t>)
                    return Encoding::toUNICODE(std::get<std::u8string>(Storage));

                if constexpr (std::is_same_v<typename T::value_type, char>)
                    return Encoding::toASCII(std::get<std::u8string>(Storage));

                if constexpr (std::is_same_v<typename T::value_type, char8_t>)
                    return std::get<std::u8string>(Storage);
            }

            // Initialize from the array.
            if constexpr (Type == Array) if (std::holds_alternative<Array_t>(Storage))
            {
                if constexpr (std::is_same_v<T, Array_t>) return std::get<Array_t>(Storage);

                T Result{};

                if constexpr (cmp::isDerived<T, std::set> || cmp::isDerived<T, std::unordered_set>)
                    for (const auto &Item : std::get<Array_t>(Storage))
                        Result.insert(Item);

                if constexpr (cmp::isDerived<T, std::vector>)
                    for (const auto &Item : std::get<Array_t>(Storage))
                        Result.emplace_back(Item);


                return Result;
            }

            // Initialize from the map.
            if constexpr (Type == Object) if (std::holds_alternative<Object_t>(Storage))
            {
                if constexpr (std::is_same_v<T, Object_t>) return std::get<Object_t>(Storage);

                T Result{};

                for (const auto &[Key, Value] : std::get<Object_t>(Storage))
                    Result.emplace(Key, Value);

                return Result;
            }

            // Improper use.
            assert(false);
            return {};
        }

        // Bracket access to objects and arrays.

        [[nodiscard]] Value_t &operator[](const std::string &Key)
        {
            if (!std::holds_alternative<Object_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Object_t>(Storage).operator[](Encoding::toUTF8(Key));
        }
        [[nodiscard]] Value_t &operator[](const std::u8string &Key)
        {
            if (!std::holds_alternative<Object_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Object_t>(Storage).operator[](Key);
        }
        [[nodiscard]] Value_t &operator[](const size_t Index)
        {
            if (!std::holds_alternative<Array_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Array_t>(Storage).operator[](Index);
        }
        [[nodiscard]] const Value_t &operator[](const std::string &Key) const
        {
            if (!std::holds_alternative<Object_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Object_t>(Storage).at(Encoding::toUTF8(Key));
        }
        [[nodiscard]] const Value_t &operator[](const std::u8string &Key) const
        {
            if (!std::holds_alternative<Object_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Object_t>(Storage).at(Key);
        }
        [[nodiscard]] const Value_t &operator[](const size_t Index) const
        {
            if (!std::holds_alternative<Array_t>(Storage)) { static Value_t Dummyvalue{}; assert(false); return Dummyvalue; }
            return std::get<Array_t>(Storage).operator[](Index);
        }

        // Validate contents.
        [[nodiscard]] bool empty() const
        {
            if (std::holds_alternative<std::u8string>(Storage)) return std::get<std::u8string>(Storage).empty();
            if (std::holds_alternative<Object_t>(Storage)) return std::get<Object_t>(Storage).empty();
            if (std::holds_alternative<Array_t>(Storage)) return std::get<Array_t>(Storage).empty();

            return true;
        }
        [[nodiscard]] bool contains(const std::string &Key) const
        {
            if (!std::holds_alternative<Object_t>(Storage)) return false;
            return std::get<Object_t>(Storage).contains(Encoding::toUTF8(Key));
        }
        [[nodiscard]] bool contains(const std::u8string &Key) const
        {
            if (!std::holds_alternative<Object_t>(Storage)) return false;
            return std::get<Object_t>(Storage).contains(Key);
        }
        template <typename ...Args> [[nodiscard]] bool contains_all(Args&&... va) const
        {
            return (contains(va) && ...);
        }
        template <typename ...Args> [[nodiscard]] bool contains_any(Args&&... va) const
        {
            return (contains(va) || ...);
        }

        // Helper for safer access to the storage.
        template <typename T> requires(std::is_convertible_v<T, Value_t>) T value(const T &Defaultvalue) const
        {
            const auto Temp = Get<T>();
            if (Temp) return *Temp;
            return Defaultvalue;
        }
        template <typename T = Value_t> requires(std::is_convertible_v<T, Value_t>) T value(const std::string &Key) const
        {
            if (!contains(Key)) return {};

            const auto Temp = operator[](Key).Get<T>();
            if (Temp) return *Temp;
            return {};
        }
        template <typename T = Value_t> requires(std::is_convertible_v<T, Value_t>) T value(const std::u8string &Key) const
        {
            if (!contains(Key)) return {};

            const auto Temp = operator[](Key).Get<T>();
            if (Temp) return *Temp;
            return {};
        }
        template <typename T> requires(std::is_convertible_v<T, Value_t>) T value(const std::string &Key, const T &Defaultvalue) const
        {
            if (!contains(Key)) return Defaultvalue;

            const auto Temp = operator[](Key).Get<T>();
            if (Temp) return *Temp;
            return Defaultvalue;
        }
        template <typename T> requires(std::is_convertible_v<T, Value_t>) T value(const std::u8string &Key, const T &Defaultvalue) const
        {
            if (!contains(Key)) return Defaultvalue;

            const auto Temp = operator[](Key).Get<T>();
            if (Temp) return *Temp;
            return Defaultvalue;
        }

        //
        Value_t() = default;
        template <typename T> requires(getEnum<T>() != Null) Value_t(const T &Value) { Set(Value); }
        template <typename T = Value_t> operator T() const { const auto Temp = Get<T>(); return Temp ? *Temp : T{}; }
        template <typename T> requires(std::is_convertible_v<T, Value_t>) Value_t &operator=(const T &Value) { Set(Value); return *this; }

        // Serialize to string.
        [[nodiscard]] std::string Dump() const
        {
            if (std::holds_alternative<Null_t>(Storage)) return "null";
            if (std::holds_alternative<double>(Storage)) return va("%f", *Get<double>());
            if (std::holds_alternative<int64_t>(Storage)) return va("%lli", *Get<int64_t>());
            if (std::holds_alternative<uint64_t>(Storage)) return va("%llu", *Get<uint64_t>());
            if (std::holds_alternative<bool>(Storage)) return *Get<bool>() ? "true" : "false";
            if (std::holds_alternative<String_t>(Storage)) return va("\"%s\"", Encoding::toASCII(*Get<std::u8string>()).c_str());

            if (std::holds_alternative<Array_t>(Storage))
            {
                std::string Result{ "[" };
                for (const auto Array = *Get<Array_t>(); const auto & Item : Array)
                {
                    Result.append(Item.Dump());
                    Result.append(",");
                }

                if (!Get<Array_t>()->empty()) Result.pop_back();
                Result.append("]");
                return Result;
            }
            if (std::holds_alternative<Object_t>(Storage))
            {
                std::string Result{ "{" };
                for (const auto Object = *Get<Object_t>(); const auto & [Key, Value] : Object)
                {
                    Result.append(va("\"%*s\":", Key.size(), Key.data()));
                    Result.append(Value.Dump());
                    Result.append(",");
                }

                if (!Get<Object_t>()->empty()) Result.pop_back();
                Result.append("}");
                return Result;
            }

            assert(false);
            return "WTF?";
        }
        [[nodiscard]] std::string dump() const
        {
            return Dump();
        }
    };

    namespace Parsing
    {
        inline Basetype_t getNext(std::u8string_view &Input)
        {
            // Skip to the type.
            while (!Input.empty() && std::ranges::contains(u8" ,:\t\n", Input[0])) Input.remove_prefix(1);
            if (Input.empty()) [[unlikely]] return Basetype_t::Null;

            // Simple case.
            const auto Token = Input[0];
            if (Token == '[') return Basetype_t::Array;
            if (Token == '{') return Basetype_t::Object;
            if (Token == '\"') return Basetype_t::String;
            if (Token == 't' || Token == 'T') return Basetype_t::Bool;
            if (Token == 'f' || Token == 'F') return Basetype_t::Bool;
            if (Token == 'n' || Token == 'N') return Basetype_t::Null;

            // Deliminator..
            if (Token == ']') return Basetype_t::Null;
            if (Token == '}') return Basetype_t::Null;

            // Deduce the type of number.
            const auto Substring = Input.substr(0, Input.find(','));
            if (std::ranges::contains(Substring, '.')) return Basetype_t::Float;
            if (std::ranges::contains(Substring, 'e')) return Basetype_t::Float;
            if (std::ranges::contains(Substring, 'E')) return Basetype_t::Float;
            if (std::ranges::contains(Substring, '-')) return Basetype_t::Signedint;

            return Basetype_t::Unsignedint;
        }

        inline Value_t Parsenull(std::u8string_view &Input)
        {
            if (Input[0] == 'n' || Input[0] == 'N')
            {
                Input.remove_prefix(sizeof("null"));
                return {};
            }

            // WTF?
            assert(false);
            return {};
        }
        inline Value_t Parseboolean(std::u8string_view &Input)
        {
            if (Input[0] == 't' || Input[0] == 'T')
            {
                Input.remove_prefix(sizeof("true"));
                return true;
            }

            if (Input[0] == 'f' || Input[0] == 'F')
            {
                Input.remove_prefix(sizeof("false"));
                return false;
            }

            // WTF?
            assert(false);
            return {};
        }
        inline Value_t Parsestring(std::u8string_view &Input)
        {
            // Sanity check.
            assert(Input[0] == '\"');
            Input.remove_prefix(1);

            // Simple case.
            auto Result = std::u8string(Input.substr(0, Input.find('\"')));
            Input.remove_prefix(Result.size());

            // Handle escaped strings.
            while ((*(Result.end() - 1) == '\\'))
            {
                const auto Substring = Input.substr(0, Input.find('\"'));
                Input.remove_prefix(Substring.size());
                Result += Substring;
            }

            // Remove terminator.
            Input.remove_prefix(1);
            return Result;
        }
        inline Value_t Parsenumber(std::u8string_view &Input)
        {
            const auto Type = getNext(Input);

            if (Type == Basetype_t::Unsignedint)
            {
                uint64_t Value;
                const auto Result = std::from_chars((const char *)Input.data(), (const char *)Input.data() + Input.size(), Value);
                if (Result.ec != std::errc{}) return {};

                Input.remove_prefix(std::uintptr_t(Result.ptr) - std::uintptr_t(Input.data()));
                return Value;
            }

            if (Type == Basetype_t::Signedint)
            {
                int64_t Value;
                const auto Result = std::from_chars((const char *)Input.data(), (const char *)Input.data() + Input.size(), Value);
                if (Result.ec != std::errc{}) return {};

                Input.remove_prefix(std::uintptr_t(Result.ptr) - std::uintptr_t(Input.data()));
                return Value;
            }

            if (Type == Basetype_t::Float)
            {
                double Value;
                const auto Result = std::from_chars((const char *)Input.data(), (const char *)Input.data() + Input.size(), Value);
                if (Result.ec != std::errc{}) return {};

                Input.remove_prefix(std::uintptr_t(Result.ptr) - std::uintptr_t(Input.data()));
                return Value;
            }

            // WTF?
            assert(false);
            return {};
        }

        inline Value_t Parseobject(std::u8string_view &Input);
        inline Value_t Parsearray(std::u8string_view &Input)
        {
            std::vector<Value_t> Result{};

            // Sanity check.
            assert(Input[0] == '[');
            Input.remove_prefix(1);

            while (true)
            {
                const auto Type = getNext(Input);

                if (Type == Basetype_t::Null && Input[0] == ']') break;

                if (Type == Basetype_t::Null) Result.emplace_back(Parsenull(Input));
                if (Type == Basetype_t::Bool) Result.emplace_back(Parseboolean(Input));

                if (Type == Basetype_t::Array) Result.emplace_back(Parsearray(Input));
                if (Type == Basetype_t::Object) Result.emplace_back(Parseobject(Input));
                if (Type == Basetype_t::String) Result.emplace_back(Parsestring(Input));

                if (Type == Basetype_t::Float) Result.emplace_back(Parsenumber(Input));
                if (Type == Basetype_t::Signedint) Result.emplace_back(Parsenumber(Input));
                if (Type == Basetype_t::Unsignedint) Result.emplace_back(Parsenumber(Input));
            }

            // Validate.
            assert(Input[0] == ']');
            Input.remove_prefix(1);

            return Result;
        }
        inline Value_t Parseobject(std::u8string_view &Input)
        {
            Object_t Result{};

            // Sanity check.
            assert(Input[0] == '{');
            Input.remove_prefix(1);

            while (true)
            {
                const auto Keytype = getNext(Input);
                if (Keytype == Basetype_t::Null) break;
                if (Keytype != Basetype_t::String) [[unlikely]] { Errorprint("JSON parsing: Expected a key."); break; }

                const std::u8string Key = Parsestring(Input);
                const auto Valuetype = getNext(Input);

                if (Valuetype == Basetype_t::Null && Input[0] == '}') break;
                if (Valuetype == Basetype_t::Null) Result.emplace(Key, Parsenull(Input));

                if (Valuetype == Basetype_t::Bool) Result.emplace(Key, Parseboolean(Input));

                if (Valuetype == Basetype_t::Array) Result.emplace(Key, Parsearray(Input));
                if (Valuetype == Basetype_t::Object) Result.emplace(Key, Parseobject(Input));
                if (Valuetype == Basetype_t::String) Result.emplace(Key, Parsestring(Input));

                if (Valuetype == Basetype_t::Float) Result.emplace(Key, Parsenumber(Input));
                if (Valuetype == Basetype_t::Signedint) Result.emplace(Key, Parsenumber(Input));
                if (Valuetype == Basetype_t::Unsignedint) Result.emplace(Key, Parsenumber(Input));
            }

            // Validate.
            assert(Input[0] == '}');
            Input.remove_prefix(1);

            return Result;
        }
    }
    inline Value_t Parse(std::u8string_view JSONString)
    {
        // Malformed statement check. Missing brackets, null-chars messing up C-string parsing.
        const auto C1 = std::ranges::count(JSONString, '{') != std::ranges::count(JSONString, '}');
        const auto C2 = std::ranges::count(JSONString, '[') != std::ranges::count(JSONString, ']');
        const auto C3 = std::ranges::count(JSONString, '\0') > 1;
        if (C1 || C2 || C3) [[unlikely]]
        {
            const auto VA = va("Trying to parse invalid JSON string, first chars: %*s", std::min(size_t(20), JSONString.size()), JSONString.data());
            Errorprint(VA);
            assert(false);
            return {};
        }

        return Parsing::Parseobject(JSONString);
    }
    inline Value_t Parse(std::string_view JSONString)
    {
        return Parse(std::u8string_view((const char8_t *)JSONString.data(), JSONString.size()));
    }

    inline std::string Dump(Value_t &&Value)
    {
        return Value.Dump();
    }
    inline std::string Dump(const Value_t &Value)
    {
        return Value.Dump();
    }

    #pragma warning(pop)
}
