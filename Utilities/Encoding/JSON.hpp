/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-07
    License: MIT
*/

#pragma once
#include "../Utilities.hpp"
#include "../Constexprhelpers.hpp"
#include "../Strings/Variadicstring.hpp"

namespace JSON
{
    #pragma warning(disable: 4702)

    enum class Type_t { Null, Bool, Float, Signedint, Unsignedint, String, Object, Array };
    using Object_t = std::unordered_map<std::string, struct Value_t>;
    using Array_t = std::vector<Value_t>;
    using String_t = std::u8string;

    // Helper for type-enum deduction.
    template <typename T> constexpr Type_t toType()
    {
        if constexpr (std::is_same_v<T, Object_t>) return Type_t::Object;
        if constexpr (std::is_same_v<T, String_t>) return Type_t::String;
        if constexpr (std::is_same_v<T, Array_t>) return Type_t::Array;

        if constexpr (cmp::isDerived<T, std::basic_string_view>) return Type_t::String;
        if constexpr (cmp::isDerived<T, std::basic_string>) return Type_t::String;
        if constexpr (cmp::isDerived<T, std::unordered_set>) return Type_t::Array;
        if constexpr (cmp::isDerived<T, std::vector>) return Type_t::Array;
        if constexpr (cmp::isDerived<T, std::set>) return Type_t::Array;

        if constexpr (cmp::isDerived<T, std::unordered_map>) return Type_t::Object;
        if constexpr (cmp::isDerived<T, std::map>) return Type_t::Object;

        if constexpr (std::is_floating_point_v<T>) return Type_t::Float;
        if constexpr (std::is_same_v<T, bool>) return Type_t::Bool;
        if constexpr (std::is_integral_v<T>)
        {
            if constexpr (std::is_signed_v<T>)
                return Type_t::Signedint;
            return Type_t::Unsignedint;
        }

        return Type_t::Null;
    }

    // Holds the value, operator ! checks Type == Null.
    struct Value_t
    {
        std::shared_ptr<void> Internalstorage{};
        Type_t Type{};

        // Convert the storage based on type.
        template <typename T> std::shared_ptr<T> asPtr()
        {
            assert(toType<T>() == Type);
            return std::static_pointer_cast<T>(Internalstorage);
        }
        template <typename T> std::shared_ptr<const T> asPtr() const
        {
            assert(toType<T>() == Type);
            return std::static_pointer_cast<const T>(Internalstorage);
        }

        // Verify contents.
        [[nodiscard]] bool contains(const std::string_view Key) const
        {
            if (Type != Type_t::Object) return false;
            return asPtr<Object_t>()->contains(Key.data()) && !asPtr<Object_t>()->at(Key.data()).isNull();
        }
        [[nodiscard]] bool isNull() const
        {
            return Type == Type_t::Null;
        }
        [[nodiscard]] bool empty() const
        {
            if (Type == Type_t::String) return asPtr<std::u8string>()->empty();
            if (Type == Type_t::Object) return asPtr<Object_t>()->empty();
            if (Type == Type_t::Array) return asPtr<Array_t>()->empty();
            return true;
        }

        // Get the contents, std::basic_string can't be deduced so need to be explicit.
        template <typename T> operator T() const
        {
            switch (Type)
            {
                case Type_t::Object: if constexpr (std::is_same_v<T, Object_t>) return *asPtr<Object_t>(); break;
                case Type_t::Unsignedint: if constexpr (std::is_integral_v<T>) return *asPtr<uint64_t>(); break;
                case Type_t::Float: if constexpr (std::is_floating_point_v<T>) return *asPtr<double>(); break;
                case Type_t::Signedint: if constexpr (std::is_integral_v<T>) return *asPtr<int64_t>(); break;
                case Type_t::Bool: if constexpr (std::is_same_v<T, bool>) return *asPtr<bool>(); break;
                case Type_t::Null: break;

                case Type_t::String:
                {
                    if constexpr (cmp::isDerived<T, std::basic_string>)
                    {
                        if constexpr (std::is_same_v<typename T::value_type, wchar_t>)
                            return Encoding::toUNICODE(*asPtr<std::u8string>());

                        if constexpr (std::is_same_v<typename T::value_type, char>)
                            return Encoding::toASCII(*asPtr<std::u8string>());

                        if constexpr (std::is_same_v<typename T::value_type, char8_t>)
                            return *asPtr<std::u8string>();
                    }
                    else
                    {
                        // While possible in the case of std::u8string_view, better to discourage this.
                        static_assert(!cmp::isDerived<T, std::basic_string_view>, "Don't try to cast to a *_view string.");
                    }
                    break;
                }

                case Type_t::Array:
                {
                    if constexpr (cmp::isDerived<T, std::set> || cmp::isDerived<T, std::unordered_set> || cmp::isDerived<T, std::vector>)
                    {
                        if constexpr (std::is_same_v<T, Array_t>) return *asPtr<Array_t>();

                        const auto Array = asPtr<Array_t>();
                        T Result; Result.reserve(Array->size());

                        if constexpr (cmp::isDerived<T, std::set> || cmp::isDerived<T, std::unordered_set>)
                            for (const auto &Item : *Array)
                                Result.insert(Item);

                        if constexpr (cmp::isDerived<T, std::vector>)
                            std::ranges::copy(*Array, Result.begin());

                        return Result;
                    }
                    break;
                }
            }

            // Allow NULL-ness to be checked in if-statements.
            if constexpr (std::is_same_v<T, bool>) return !isNull();
            else return {};
        }
        template <typename T> T get() const
        {
            return this->operator T();
        }

        // Sub-object access.
        template <size_t N>
        auto operator[](const char(&Key)[N]) const
        {
            if (Type != Type_t::Object) { static Value_t Dummyvalue{}; return Dummyvalue; }
            return asPtr<Object_t>()->operator[]({ Key, N });
        }
        auto operator[](size_t N) const
        {
            if (Type != Type_t::Array) { static Value_t Dummyvalue{}; return Dummyvalue; }
            return asPtr<Array_t>()->operator[](N);
        }
        auto operator[](std::string_view Key)
        {
            if (Type != Type_t::Object) { static Value_t Dummyvalue{}; return Dummyvalue; }
            return asPtr<Object_t>()->operator[](std::unordered_map<std::string, Value_t>::key_type(Key));
        }

        // Multi-verify.
        template <typename ...Args> [[nodiscard]] bool contains_all(Args&&... va) const
        {
            if (Type != Type_t::Object) return false;
            return (contains(va) && ...);
        }
        template <typename ...Args> [[nodiscard]] bool contains_any(Args&&... va) const
        {
            if (Type != Type_t::Object) return false;
            return (contains(va) || ...);
        }

        // Safe access with a default value.
        template <typename T, size_t N> auto value(std::string_view Key, const std::array<T, N> &Defaultvalue)
        {
            return value(Key, std::basic_string<std::remove_const_t<T>>(Defaultvalue, N));
        }
        template <typename T, size_t N> auto value(std::string_view Key, T(&Defaultvalue)[N])
        {
            return value(Key, std::basic_string<std::remove_const_t<T>>(Defaultvalue, N));
        }
        template <typename T> T value(const std::string_view Key, T Defaultvalue) const
        {
            if constexpr (!std::is_convertible_v<Value_t, T>) return Defaultvalue;
            if (Type != Type_t::Object) return Defaultvalue;

            if (!asPtr<Object_t>()->contains(Key.data())) return Defaultvalue;
            return asPtr<Object_t>()->at(Key.data());
        }
        template <typename T = Value_t> T value(const std::string_view Key) const
        {
            if constexpr (!std::is_convertible_v<Value_t, T>) return {};
            if (Type != Type_t::Object) return {};

            if (!asPtr<Object_t>()->contains(Key.data())) return {};
            return asPtr<Object_t>()->at(Key.data());
        }

        // Initialize for most things.
        template <typename T> Value_t(const T &Input) requires(!std::is_pointer_v<T>)
        {
            // Optional support.
            if constexpr (cmp::isDerived<T, std::optional>)
            {
                if (Input) *this = Value_t(*Input);
                else *this = Value_t();
                return;
            }
            else
            {
                // Safety-check..
                static_assert(toType<T>() != Type_t::Null);
            }

            if constexpr (std::is_same_v<T, Value_t>) *this = Input;
            else if constexpr (std::is_same_v<T, Object_t>)
            {
                Internalstorage = std::make_shared<Object_t>(Input);
                Type = Type_t::Object;
                return;
            }
            else if constexpr (std::is_same_v<T, Array_t>)
            {
                Internalstorage = std::make_shared<Array_t>(Input);
                Type = Type_t::Array;
                return;
            }
            else
            {
                Type = toType<T>();

                if constexpr (toType<T>() == Type_t::String) Internalstorage = std::make_shared<std::u8string>(Encoding::toUTF8(Input));
                if constexpr (toType<T>() == Type_t::Unsignedint) Internalstorage = std::make_shared<uint64_t>(Input);
                if constexpr (toType<T>() == Type_t::Signedint) Internalstorage = std::make_shared<int64_t>(Input);
                if constexpr (toType<T>() == Type_t::Float) Internalstorage = std::make_shared<double>(Input);
                if constexpr (toType<T>() == Type_t::Bool) Internalstorage = std::make_shared<bool>(Input);

                if constexpr (cmp::isDerived<T, std::unordered_map> || cmp::isDerived<T, std::map>)
                {
                    static_assert(std::is_convertible_v<typename T::key_type, std::string>);

                    Object_t Object(Input.size());
                    for (const auto &[Key, _Value] : Input)
                        Object[Key] = _Value;

                    Type = Type_t::Object;
                    Internalstorage = std::make_shared<Object_t>(std::move(Object));
                }
                if constexpr (cmp::isDerived<T, std::set> || cmp::isDerived<T, std::unordered_set> || cmp::isDerived<T, std::vector>)
                {
                    Array_t Array(Input.size());
                    for (auto [Index, _Value] : Enumerate(Input))
                        Array[Index] = _Value;

                    Type = Type_t::Array;
                    Internalstorage = std::make_shared<Array_t>(std::move(Array));
                }
            }
        }
        template <typename T, size_t N> Value_t(const std::array<T, N> &Input)
        {
            *this = std::basic_string<std::remove_const_t<T>>(Input.data(), N);
            return;
        }
        template <typename T> Value_t(const std::basic_string<T> &Input)
        {
            Type = Type_t::String;
            Internalstorage = std::make_shared<std::u8string>(Encoding::toUTF8(Input));
        }
        template <typename T, size_t N> Value_t(T(&Input)[N])
        {
            *this = std::basic_string<std::remove_const_t<T>>(Input, N);
            return;
        }
        Value_t() = default;

        // Serialize to a string.
        [[nodiscard]] std::string to_string() const
        {
            switch (Type)
            {
                case Type_t::String: return va("\"%s\"", Encoding::toASCII(*asPtr<std::u8string>()).c_str());
                case Type_t::Unsignedint: return va("%llu", *asPtr<uint64_t>());
                case Type_t::Signedint: return va("%lli", *asPtr<int64_t>());
                case Type_t::Bool: return *asPtr<bool>() ? "true" : "false";
                case Type_t::Float: return va("%f", *asPtr<double>());
                case Type_t::Null: return "null";

                case Type_t::Object:
                {
                    std::string Result{ "{" };
                    for (const auto Ptr = asPtr<Object_t>(); const auto &[lKey, lValue] : *Ptr)
                    {
                        Result.append(va("\"%*s\":", lKey.size(), lKey.data()));
                        Result.append(lValue.to_string());
                        Result.append(",");
                    }
                    if (!asPtr<Object_t>()->empty()) Result.pop_back();
                    Result.append("}");
                    return Result;
                }
                case Type_t::Array:
                {
                    std::string Result{ "[" };
                    for (const auto Ptr = asPtr<Array_t>(); const auto &lValue : *Ptr)
                    {
                        Result.append(lValue.to_string());
                        Result.append(",");
                    }
                    if (!asPtr<Array_t>()->empty()) Result.pop_back();
                    Result.append("]");
                    return Result;
                }
            }

            return {};
        }
    };

    // Friend access for dumping complex values.
    inline std::string Dump(const Value_t &Value) { return Value.to_string(); }
    inline std::string Dump(const Array_t &Value) { return Value_t(Value).to_string(); }
    inline std::string Dump(const Object_t &Value) { return Value_t(Value).to_string(); }

    // Parse via NLohmann, TODO(tcn): add a fallback lexer.
    inline Value_t Parsestring(std::string_view JSONString)
    {
        Value_t Result{};

        // Malformed statement check. Missing brackets, null-chars messing up C-string parsing.
        const auto C1 = std::ranges::count(JSONString, '{') != std::ranges::count(JSONString, '}');
        const auto C2 = std::ranges::count(JSONString, '[') != std::ranges::count(JSONString, ']');
        const auto C3 = std::ranges::count(JSONString, '\0') > 1;
        if (C1 || C2 || C3) [[unlikely]]
        {
            Errorprint(va("Trying to parse invalid JSON string, first chars: %*s", std::min(size_t(20), JSONString.size()), JSONString.data()));
            return Result;
        }

        // Implementation dependent.
        if (!JSONString.empty())
        {
            #if defined(HAS_NLOHMANN)

            const std::function<Value_t(const nlohmann::json &)> Parse = [&Parse](const nlohmann::json &Item) -> Value_t
            {
                if (Item.is_string()) return Encoding::toUTF8(Item.get<std::string>());
                if (Item.is_number_unsigned()) return Item.get<uint64_t>();
                if (Item.is_number_integer()) return Item.get<int64_t>();
                if (Item.is_number_float()) return Item.get<double>();
                if (Item.is_boolean()) return Item.get<bool>();
                if (Item.is_object())
                {
                    Object_t Object; Object.reserve(Item.size());
                    for (auto Items = Item.items(); const auto &[Key, Value] : Items)
                    {
                        Object.emplace(Key, Parse(Value));
                    }
                    return Object;
                }
                if (Item.is_array())
                {
                    Array_t Array; Array.reserve(Item.size());
                    for (const auto &Subitem : Item)
                    {
                        Array.push_back(Parse(Subitem));
                    }
                    return Array;
                }

                // WTF??
                assert(false);
                return {};
            };

            try { Result = Parse(nlohmann::json::parse(JSONString.data(), nullptr, true, true)); }
            catch (const std::exception &e) { (void)e; Debugprint(e.what()); };

            #else

            static_assert(false, "No JSON parser available.");
            #endif
        }

        return Result;
    }
    inline Value_t Parsestring(const std::string &JSONString)
    {
        if (JSONString.empty()) return {};
        return Parsestring(std::string_view(JSONString));
    }
    template <cmp::Byte_t T> Value_t Parsestring(std::span<T> Span)
    {
        return Parsestring(std::string_view{ (char *)Span.data(), Span.size() });
    }
    inline Value_t Parsestring(const char *JSONString, const size_t Length = 0)
    {
        assert(JSONString);
        if (Length == 0) return Parsestring(std::string(JSONString));
        return Parsestring(std::string_view(JSONString, Length));
    }
}
