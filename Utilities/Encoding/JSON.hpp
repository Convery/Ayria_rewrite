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
    #pragma warning(disable : 4456)

    using Object_t = std::unordered_map<std::u8string, struct Value_t>;
    using Array_t = std::vector<struct Value_t>;
    using String_t = std::u8string;
    using Null_t = std::monostate;
    using Unsigned_t = uint64_t;
    using Signed_t = int64_t;
    using Number_t = double;
    using Boolean_t = bool;

    template <typename T, typename... U> concept Any_t = (std::same_as<T, U> || ...);
    template <typename T> concept Supported_t = Any_t<T, Null_t, Boolean_t, Number_t, Signed_t, Unsigned_t, Object_t, Array_t, String_t>;

    #if defined (__cpp_lib_variant) && __cpp_lib_variant >= 202106L
        #define CXPR constexpr
    #else
        #define CXPR
    #endif

    // Encode strings in the format the user wants.
    namespace Internal {
        template <typename T> constexpr T toString(const String_t &Value) {
            if constexpr (std::is_same_v<typename T::value_type, wchar_t>) return Encoding::toUNICODE(Value);
            else if constexpr (std::is_same_v<typename T::value_type, char>) return Encoding::toASCII(Value);
            else if constexpr (std::is_same_v<typename T::value_type, char8_t>) return Value;

            else static_assert(cmp::always_false<T>, "Invalid string type, Blob_t should be handled elsewhere.");

            std::unreachable();
        }
    }

    //
    struct Value_t
    {
        std::variant<Null_t, Boolean_t, Number_t, Signed_t, Unsigned_t, Object_t, Array_t, String_t> Storage{ Null_t{} };
        template <Supported_t T> CXPR bool isType() const { return std::holds_alternative<T>(Storage); }

        // Direct access to the stored value (if available), else to a default-constructed element.
        template <Supported_t T> CXPR operator const T&() const { if (isType<T>()) return std::get<T>(Storage); static T Dummy{}; return Dummy; }
        template <Supported_t T> CXPR operator T &() { if (isType<T>()) return std::get<T>(Storage); static T Dummy{}; return Dummy; }

        // Try to convert to compatible types, Get function for explicit access.
        template <typename T> requires(!Supported_t<T>) CXPR operator T() const
        {
            // POD
                 if constexpr (std::is_same_v<T, bool>) { if (isType<Boolean_t>()) return T(std::get<Boolean_t>(Storage)); return T{}; }
            else if constexpr (std::is_floating_point_v<T>) { if (isType<Number_t>()) return T(std::get<Number_t>(Storage)); return T{}; }
            else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) { if (isType<Signed_t>()) return T(std::get<Signed_t>(Storage)); return T{}; }
            else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) { if (isType<Unsigned_t>()) return T(std::get<Unsigned_t>(Storage)); return T{}; }

            // Blob_t derives from basic_string, so it needs to be checked first.
            else if constexpr (std::is_same_v<T, Blob_t> || cmp::isDerived<T, std::unordered_set> || cmp::isDerived<T, std::vector> || cmp::isDerived<T, std::set>)
            {
                if (!isType<Array_t>()) return T{};
                return T(std::get<Array_t>(Storage).begin(), std::get<Array_t>(Storage).end());
            }
            else if constexpr (cmp::isDerived<T, std::basic_string> || cmp::isDerived<T, std::pmr::basic_string>)
            {
                if (!isType<String_t>()) return T{};
                return Internal::toString<T>(std::get<String_t>(Storage));
            }

            // Maps need to be checked last due to typename T::key_type always being considered.
            else if constexpr ((cmp::isDerived<T, std::unordered_map> || cmp::isDerived<T, std::map>) && cmp::isDerived<typename T::key_type, std::basic_string>)
            {
                if (!isType<Object_t>()) return T{};
                T Output{};

                for (const auto &[Key, Value] : std::get<Object_t>(Storage))
                    Output.emplace(Internal::toString<typename T::key_type>(Key), Value);

                return Output;
            }

            else static_assert(cmp::always_false<T>, "Could not convert JSON value to T");

            std::unreachable();
        }
        template <typename T> CXPR T Get() const { return static_cast<T>(*this); }

        // Convert to a supported type.
        Value_t() = default;
        template <Supported_t T> CXPR Value_t(const T &Value) : Storage(Value) {}
        template <typename T> CXPR Value_t(const T &Value)
        {
            if constexpr (cmp::isDerived<T, std::optional>)
            {
                if (Value) *this = Value_t(*Value);
                else Storage = Null_t{};
            }
            else if constexpr (std::is_same_v<T, Value_t>)
            {
                Storage = Value.Storage;
            }
            else
            {
                Storage = [](const auto &Value)
                {
                    // Generally POD.
                         if constexpr (std::is_floating_point_v<T>) return Number_t{ Value };
                    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Signed_t{ Value };
                    else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) return Unsigned_t{ Value };

                    // Blob_t derives from basic_string, so it needs to be checked first.
                    else if constexpr (std::is_same_v<T, Blob_t> || cmp::isDerived<T, std::unordered_set> || cmp::isDerived<T, std::vector> || cmp::isDerived<T, std::set>)
                    {
                        Array_t Output{};
                        for (const auto &Item : Value)
                            Output.emplace_back(Item);
                        return Output;
                    }
                    else if constexpr (cmp::isDerived<T, std::basic_string> || cmp::isDerived<T, std::basic_string_view>)
                    {
                        return String_t{ Encoding::toUTF8(Value) };
                    }

                    // Maps need to be checked last due to typename T::key_type always being considered.
                    else if constexpr ((cmp::isDerived<T, std::unordered_map> || cmp::isDerived<T, std::map>) && cmp::isDerived<typename T::key_type, std::basic_string>)
                    {
                        Object_t Output{};
                        for (const auto &[Key, Item] : Value)
                            Output.emplace(Encoding::toUTF8(Key), Item);
                        return Output;
                    }
                    else static_assert(cmp::always_false<T>, "Could not convert T to JSON value");

                    std::unreachable();
                }(Value);
            }
        }

        // Provides a default constructed value on error.
        Value_t &operator[](size_t i)
        {
            if (isType<Array_t>()) return std::get<Array_t>(Storage)[i];
            else { static Value_t Dummy{}; return Dummy; }
        }
        Value_t &operator[](const String_t &Key)
        {
            if (isType<Object_t>()) return std::get<Object_t>(Storage)[Key];
            else { static Value_t Dummy{}; return Dummy; }
        }
        Value_t &operator[](const std::string &Key)
        {
            if (isType<Object_t>()) return std::get<Object_t>(Storage)[Encoding::toUTF8(Key)];
            else { static Value_t Dummy{}; return Dummy; }
        }
        const Value_t &operator[](size_t i) const
        {
            if (isType<Array_t>()) return std::get<Array_t>(Storage)[i];
            else { static Value_t Dummy{}; return Dummy; }
        }
        const Value_t &operator[](const String_t &Key) const
        {
            if (isType<Object_t>())
            {
                const auto &Object = std::get<Object_t>(Storage);
                if (Object.contains(Key)) return Object.at(Key);
            }

            static Value_t Dummy{}; return Dummy;
        }
        const Value_t &operator[](const std::string &Key) const
        {
            if (isType<Object_t>())
            {
                const auto &Object = std::get<Object_t>(Storage);
                if (Object.contains(Encoding::toUTF8(Key)))
                    return Object.at(Encoding::toUTF8(Key));
            }

            static Value_t Dummy{}; return Dummy;
        }

        // Get storage safely.
        template <typename T = Value_t> requires(std::is_convertible_v<Value_t, T>)
        CXPR T value(const String_t &Key, const T &Defaultvalue = {}) const
        {
            if (const auto Value = operator[](Key))
                return Value;
            return Defaultvalue;
        }
        template <typename T = Value_t> requires(std::is_convertible_v<Value_t, T>)
        CXPR T value(const std::string &Key, const T &Defaultvalue = {}) const
        {
            if (const auto Value = operator[](Key))
                return Value;
            return Defaultvalue;
        }
        template <typename T = Value_t> requires(std::is_convertible_v<Value_t, T>)
        CXPR T value(const T &Defaultvalue = {}) const
        {
            if (isType<T>()) return Get<T>();
            return Defaultvalue;
        }

        // Helpers for objects.
        CXPR bool empty() const
        {
            if (isType<Object_t>()) return std::get<Object_t>(Storage).empty();
            if (isType<String_t>()) return std::get<String_t>(Storage).empty();
            if (isType<Array_t>()) return std::get<Array_t>(Storage).empty();

            return true;
        }
        CXPR bool contains(const String_t &Key) const
        {
            if (!isType<Object_t>()) return false;
            return std::get<Object_t>(Storage).contains(Key);
        }
        CXPR bool contains(const std::string &Key) const
        {
            if (!isType<Object_t>()) return false;
            return std::get<Object_t>(Storage).contains(Encoding::toUTF8(Key));
        }
        template <typename ...Args> bool contains_all(Args&&... va) const
        {
            return (contains(va) && ...);
        }
        template <typename ...Args> bool contains_any(Args&&... va) const
        {
            return (contains(va) || ...);
        }

        // Serialize to string.
        std::string dump() const
        {
            if (isType<Null_t>()) return "null";
            if (isType<Number_t>()) return va("%f", Get<Number_t>());
            if (isType<Signed_t>()) return va("%lli", Get<Signed_t>());
            if (isType<Unsigned_t>()) return va("%llu", Get<Unsigned_t>());
            if (isType<Boolean_t>()) return Get<Boolean_t>() ? "true" : "false";
            if (isType<String_t>()) return va("\"%s\"", Encoding::toASCII(Get<String_t>()));

            if (isType<Array_t>())
            {
                std::string Result{ "[" };
                for (const auto &Item : Get<Array_t>())
                {
                    Result.append(Item.dump());
                    Result.append(" ,");
                }

                if (!Get<Array_t>().empty()) Result.pop_back();
                Result.append("]");
                return Result;
            }
            if (isType<Object_t>())
            {
                std::string Result{ "{" };
                for (const auto &[Key, Value] : Get<Object_t>())
                {
                    Result.append(va("\"%s\" : ", Encoding::toASCII(Key)));
                    Result.append(Value.dump());
                    Result.append(" ,");
                }

                if (!Get<Object_t>().empty()) Result.pop_back();
                Result.append("}");
                return Result;
            }

            std::unreachable();
        }
    };

    // Naive parsing.
    namespace Parsing
    {
        constexpr std::u8string_view Skip(std::u8string_view Input)
        {
            auto View = Input | std::views::drop_while([](auto Char) { return std::isspace(static_cast<uint8_t>(Char)); });
            return std::u8string_view(View.begin(), View.end());
        }

        CXPR inline std::optional<Value_t> Parsevalue(std::u8string_view &Input);
        CXPR inline std::optional<Array_t> Parsearray(std::u8string_view &Input)
        {
            Array_t Result{};

            // How did we even get here?
            if (Input.empty() || Input.front() != u8'[')
                return {};

            Input.remove_prefix(1); Input = Skip(Input);
            while (!Input.empty() && Input.front() != u8']')
            {
                if (const auto Value = Parsevalue(Input))
                    Result.emplace_back(*Value);
                else return {};

                Input = Skip(Input);
                if (Input.empty() || (Input.front() != u8',' && Input.front() != u8']'))
                    return {};

                if (Input.front() == u8',') Input.remove_prefix(1);
                Input = Skip(Input);
            }

            if (Input.empty() || Input.front() != u8']')
                return {};

            Input.remove_prefix(1);
            return Result;
        }
        CXPR inline std::optional<String_t> Parsestring(std::u8string_view &Input)
        {
            String_t Result{};

            // How did we even get here?
            if (Input.empty() || Input.front() != u8'"')
                return {};

            Input.remove_prefix(1);
            while (!Input.empty() && Input.front() != u8'"')
            {
                if (Input.front() == u8'\\')
                    Input.remove_prefix(1);

                Result.push_back(Input.front());
                Input.remove_prefix(1);
            }

            // No endtoken.
            if (Input.empty() || Input.front() != u8'"')
                return {};

            Input.remove_prefix(1);
            return Result;
        }
        CXPR inline std::optional<Object_t> Parseobject(std::u8string_view &Input)
        {
            Object_t Result{};

            // How did we even get here?
            if (Input.empty() || Input.front() != u8'{')
                return {};

            Input.remove_prefix(1); Input = Skip(Input);
            while (!Input.empty() && Input.front() != u8'}')
            {
                const auto Key = Parsestring(Input);
                if (!Key) return {};

                Input = Skip(Input);
                if (Input.empty() || Input.front() != u8':')
                    return {};

                Input.remove_prefix(1);
                if (const auto Value = Parsevalue(Input))
                    Result.emplace(*Key, *Value);
                else return {};

                Input = Skip(Input);
                if (Input.empty() || (Input.front() != u8',' && Input.front() != u8'}'))
                    return {};

                if (Input.front() == u8',') Input.remove_prefix(1);
                Input = Skip(Input);
            }

            if (Input.empty() || Input.front() != u8'}')
                return {};

            Input.remove_prefix(1);
            return Result;
        }

        // Only function that can be called safely from usercode.
        CXPR inline std::optional<Value_t> Parsevalue(std::u8string_view &Input)
        {
            Value_t Result{};
            Input = Skip(Input);

            // How did we even get here?
            if (Input.empty()) return {};

            if (Input.starts_with(u8"null"))
            {
                Result = Null_t{};
                Input.remove_prefix(4);
            }
            else if (Input.front() == u8'"')
            {
                const auto Value = Parsestring(Input);
                if (!Value) return {};
                Result = *Value;
            }
            else if (Input.front() == u8'{')
            {
                const auto Value = Parseobject(Input);
                if (!Value) return {};
                Result = *Value;
            }
            else if (Input.front() == u8'[')
            {
                const auto Value = Parsearray(Input);
                if (!Value) return {};
                Result = *Value;
            }
            else if (Input.starts_with(u8"true"))
            {
                Result = Boolean_t{ true };
                Input.remove_prefix(4);
            }
            else if (Input.starts_with(u8"false"))
            {
                Result = Boolean_t(false);
                Input.remove_prefix(5);
            }
            else if (std::isdigit(Input.front()) || Input.front() == u8'-')
            {
                Unsigned_t Unsigned = 0; Signed_t Signed = 0; Number_t Number = 0;

                if (const auto [Ptr, ec] = std::from_chars(reinterpret_cast<const char *>(Input.data()), reinterpret_cast<const char *>(Input.data() + Input.size()), Unsigned); ec == std::errc())
                { Result = Unsigned; Input.remove_prefix(Ptr - reinterpret_cast<const char *>(Input.data())); }
                else if (const auto [Ptr, ec] = std::from_chars(reinterpret_cast<const char *>(Input.data()), reinterpret_cast<const char *>(Input.data() + Input.size()), Signed); ec == std::errc())
                { Result = Signed; Input.remove_prefix(Ptr - reinterpret_cast<const char *>(Input.data())); }
                else if (const auto [Ptr, ec] = std::from_chars(reinterpret_cast<const char *>(Input.data()), reinterpret_cast<const char *>(Input.data() + Input.size()), Number); ec == std::errc())
                { Result = Number; Input.remove_prefix(Ptr - reinterpret_cast<const char *>(Input.data())); }
                else return {};
            }
            else
                return {};

            return Result;
        }
    }

    // Pretty basic safety checks.
    inline Value_t Parse(std::u8string_view JSONString)
    {
        // Malformed string check. Missing brackets, null-chars messing up C-string parsing, etc..
        const auto C1 = std::ranges::count(JSONString, '{') != std::ranges::count(JSONString, '}');
        const auto C2 = std::ranges::count(JSONString, '[') != std::ranges::count(JSONString, ']');
        const auto C3 = std::ranges::count(JSONString, '\0') > 1;
        if (C1 || C2 || C3) [[unlikely]]
        {
            if (C1) Errorprint("Trying to parse invalid JSON string, missing }");
            if (C2) Errorprint("Trying to parse invalid JSON string, missing ]");
            if (C3) Errorprint("Trying to parse invalid JSON string, null-chars in string");

            assert(false);
            return {};
        }

        const auto Original = JSONString;
        const auto Value = Parsing::Parsevalue(JSONString);
        if (!Value)
        {
            Errorprint(va("JSON Parsing failed at position: %zu", JSONString.data() - Original.data()));
            assert(false);
            return {};
        }

        return Value;
    }
    inline Value_t Parse(std::string_view JSONString)
    {
        return Parse(std::u8string_view((const char8_t *)JSONString.data(), JSONString.size()));
    }

    // Mainly for use with objects and arrays.
    inline std::string Dump(Value_t &&Value)
    {
        return Value.dump();
    }
    inline std::string Dump(const Value_t &Value)
    {
        return Value.dump();
    }

    #pragma warning(pop)
}
