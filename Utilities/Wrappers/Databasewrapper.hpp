/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-08-27
    License: MIT

    Largely inspired by https://github.com/SqliteModernCpp/sqlite_modern_cpp

    NOTES:
    On errors, an assert is triggered.
    If a query fails, no output is written.
    Bound values per query is limited to 256.
    Only a single statement per query is supported.
    If a query returns incompatible data, output is default constructed or converted (e.g. int <-> string).
    If an output is not provided, the query will be executed when the object goes out of scope but results are discarded.

    Database_t(*Ptr) << "SELECT * FROM Table WHERE Input = ?;" << myInput
    (1)   >> [](int &a, &b, &c) -> bool {};     // Lambda with the expected types, return false to stop evaluation.
    (2)   >> [](int &a, &b, &c) -> void {};     // Lambda with the expected types, executed for each row.
    (3)   >> std::tie(a, b);                    // Tuple with the expected types.
    (4)   >> myOutput;                          // Single variable.
*/

#pragma once
#include <Utilities/Utilities.hpp>
#include "sqlite3.h"

namespace sqlite
{
    // Ignore warnings about unreachable code.
    #pragma warning(push)
    #pragma warning(disable: 4702)

    // Helpers for type deduction.
    template <class, template <class...> class> inline constexpr bool isDerived = false;
    template <template <class...> class T, class... Args> inline constexpr bool isDerived<T<Args...>, T> = true;

    template <typename T> concept Integer_t = std::is_integral_v<T>;
    template <typename T> concept Float_t = std::is_floating_point_v<T>;
    template <typename T> concept Optional_t = isDerived<T, std::optional>;
    template <typename T> concept String_t = isDerived<T, std::basic_string> || isDerived<T, std::basic_string_view>;
    template <typename T> concept Blob_t = isDerived<T, std::vector> || isDerived<T, std::set> || isDerived<T, std::unordered_set>;
    template <typename T> concept Value_t = Integer_t<T> || String_t<T> || Optional_t<T> || Blob_t<T> || Float_t<T> || std::is_same_v<T, nullptr_t> ;

    // Helper to get the arguments to a function.
    template <typename> struct Functiontraits;
    template <typename F> struct Functiontraits : public Functiontraits<decltype(&std::remove_reference_t<F>::operator())> {};
    template <typename R, typename C, typename ...Args> struct Functiontraits<R(C::*)(Args...)> : Functiontraits<R(*)(Args...)> {};
    template <typename R, typename C, typename ...Args> struct Functiontraits<R(C::*)(Args...) const> : Functiontraits<R(*)(Args...)> {};
    template <typename R, typename ...Args> struct Functiontraits<R(*)(Args...)>
    {
        static const size_t argcount = sizeof...(Args);
        using type = std::function<R>(Args...);
        using return_type = R;

        template <size_t N> using arg = std::tuple_element_t<N, std::tuple<Args...>>;
    };

    // Convert between SQL and C++.
    template <Value_t T> T getResult(sqlite3_stmt *Statement, int Index)
    {
        // The caller needs to assume ownership of the data, so views are illegal.
        static_assert(!isDerived<T, std::basic_string_view>, "Binding to a view is illegal.");

        // NULL data = default initialization rather than an error..
        const auto Type = sqlite3_column_type(Statement, Index);
        if (SQLITE_NULL == Type) return {};

        // Recurse with the actual type.
        if constexpr (Optional_t<T>)
        {
            return { getResult<typename T::value_type>(Statement, Index) };
        }

        switch (Type)
        {
            // SQLite handles the conversion.
            case SQLITE_INTEGER:
            case SQLITE_FLOAT:
                if constexpr (Integer_t<T>) return sqlite3_column_int64(Statement, Index);
                if constexpr (Float_t<T>) return sqlite3_column_double(Statement, Index);
                break;

            case SQLITE_TEXT:
                if constexpr (isDerived<T, std::basic_string>)
                {
                    // NOTE(tcn): SQLite-strings are null-terminated, but requesting the size is faster than a scan.
                    const auto Buffer = (const char8_t *)sqlite3_column_text(Statement, Index);
                    const auto Size = sqlite3_column_bytes(Statement, Index);
                    const auto Span = std::u8string_view(Buffer, Size);

                    // Format the way the caller likes it..
                    if constexpr (std::is_same_v<typename T::value_type, char>) return Encoding::toASCII(Span);
                    if constexpr (std::is_same_v<typename T::value_type, char8_t>) return Encoding::toUTF8(Span);
                    if constexpr (std::is_same_v<typename T::value_type, wchar_t>) return Encoding::toUNICODE(Span);
                }
                break;

            case SQLITE_BLOB:
                if constexpr (Blob_t<T>)
                {
                    // Not going to bother with strings as there's no universal way to handle them.
                    static_assert(!isDerived<typename T::value_type, std::basic_string>, "A BLOB of strings is not supported.");

                    const auto Size = sqlite3_column_bytes(Statement, Index);
                    const auto Elements = Size / sizeof(typename T::value_type);
                    const auto Buffer = (typename T::value_type *)sqlite3_column_blob(Statement, Index);

                    return T(Buffer, Elements);
                }
                if constexpr (String_t<T>)
                {
                    const auto Size = sqlite3_column_bytes(Statement, Index);
                    const auto Elements = Size / sizeof(typename T::value_type);
                    const auto Buffer = (typename T::value_type *)sqlite3_column_blob(Statement, Index);

                    return T(Buffer, Elements);
                }
                break;
        }

        // No acceptable conversion possible.
        assert(false);
        return {};
    }
    template <Value_t T> void getResult(sqlite3_stmt *Statement, int Index, T &Output)
    {
        Output = getResult<T>(Statement, Index);
    }
    template <Value_t T> void bindValue(sqlite3_stmt *Statement, int Index, const T &Value)
    {
        [[maybe_unused]] const auto Result = [&]() -> int
        {
            if constexpr (Blob_t<T>)
            {
                // A BLOB needs to be contigious memory.
                if constexpr (isDerived<T, std::set> || isDerived<T, std::unordered_set>)
                {
                    const std::vector<typename T::value_type> Temp(Value.begin(), Value.end());
                    return sqlite3_bind_blob(Statement, Index, Temp.data(), int(Temp.size() * sizeof(typename T::value_type)), SQLITE_TRANSIENT);
                }
                else
                {
                    return sqlite3_bind_blob(Statement, Index, Value.data(), int(Value.size() * sizeof(typename T::value_type)), SQLITE_TRANSIENT);
                }
            }
            if constexpr (std::is_same_v<T, ::Blob_t>)
            {
                return sqlite3_bind_blob(Statement, Index, Value.data(), int(Value.size()), SQLITE_TRANSIENT);
            }

            if constexpr (Integer_t<T> && sizeof(T) == sizeof(uint64_t)) return sqlite3_bind_int64(Statement, Index, Value);
            if constexpr (Integer_t<T> && sizeof(T) != sizeof(uint64_t)) return sqlite3_bind_int(Statement, Index, Value);
            if constexpr (std::is_same_v<T, nullptr_t>) return sqlite3_bind_null(Statement, Index);
            if constexpr (Float_t<T>) return sqlite3_bind_double(Statement, Index, Value);
            if constexpr (String_t<T>)
            {
                // Format the way the caller likes it..
                if constexpr (std::is_same_v<typename T::value_type, char>)    return sqlite3_bind_text(Statement, Index, Value.data(), int(Value.size()), SQLITE_TRANSIENT);
                if constexpr (std::is_same_v<typename T::value_type, wchar_t>) return sqlite3_bind_text16(Statement, Index, Value.data(), int(Value.size()), SQLITE_TRANSIENT);
                if constexpr (std::is_same_v<typename T::value_type, char8_t>) return sqlite3_bind_text(Statement, Index, (const char *)Value.data(), int(Value.size()), SQLITE_TRANSIENT);
            }

            if constexpr (Optional_t<T>)
            {
                if (Value) return bindValue(Statement, Index, *Value);
                else return sqlite3_bind_null(Statement, Index);
            }

            // Should never happen.
            return SQLITE_ERROR;
        }();

        assert(SQLITE_OK == Result);
    }

    // Helper to iterate over tuple members.
    template <typename T, size_t Index = 0, bool Last = (std::tuple_size_v<T> == Index)> struct Tuple_t
    {
        static void iterate(sqlite3_stmt *Statement, T &Tuple)
        {
            getResult(Statement, Index, std::get<Index>(Tuple));
            Tuple_t<T, Index + 1>::iterate(Statement, Tuple);
        }
    };
    template <typename T, size_t Index> struct Tuple_t<T, Index, true> { static void iterate(sqlite3_stmt *Statement, T &Tuple) {} };

    // Helper for callbacks.
    template <size_t Count> struct Functionbinder_t
    {
        template <typename Function, size_t Index> using Argtype = typename Functiontraits<Function>::template arg<Index>;
        template <typename Function> using R = typename Functiontraits<Function>::return_type;

        template <typename Function, typename ...Values, size_t Boundary = Count>
        static R<Function> Run(sqlite3_stmt *Statement, Function &&Func, Values&& ...va) requires(sizeof...(Values) < Boundary)
        {
            std::remove_cv_t<std::remove_reference_t<Argtype<Function, sizeof...(Values)>>> value{};

            getResult(Statement, sizeof...(Values), value);

            return Run<Function>(Statement, Func, std::forward<Values>(va)..., std::move(value));
        }

        template <typename Function, typename ...Values, size_t Boundary = Count>
        static R<Function> Run(sqlite3_stmt *, Function &&Func, Values&& ...va) requires(sizeof...(Values) == Boundary)
        {
            return Func(std::move(va)...);
        }
    };

    // Holds the prepared statement that we append values to.
    #pragma pack(push, 1)
    class Statement_t
    {
        std::shared_ptr<sqlite3_stmt> Statement{};
        uint8_t Argcount{};
        uint8_t Index{};

        // Step through the query and extract anything interesting.
        void Extractsingle(const std::function<void()> &&Callback) noexcept
        {
            // Verify that we didn't forget an argument.
            assert(Argcount == Index);

            // Extract a row.
            auto Result = sqlite3_step(Statement.get());
            if (SQLITE_ROW == Result) Callback();

            // Verify that there's indeed only one row.
            Result = sqlite3_step(Statement.get());

            // We do a little bit of debugging..
            if (Result != SQLITE_DONE) [[unlikely]]
            {
                const auto Error = sqlite3_errmsg(sqlite3_db_handle(Statement.get()));
                Errorprint(Error);
                assert(false);
            }

            // Reset the index for a future run.
            Index = 0;
        }
        void Extractmultiple(const std::function<void()> &&Callback) noexcept
        {
            // Verify that we didn't forget an argument.
            assert(Argcount == Index);

            auto Result = sqlite3_step(Statement.get());
            while (SQLITE_ROW == Result)
            {
                Callback();
                Result = sqlite3_step(Statement.get());
            }

            // We do a little bit of debugging..
            if (Result != SQLITE_DONE) [[unlikely]]
            {
                const auto Error = sqlite3_errmsg(sqlite3_db_handle(Statement.get()));
                Errorprint(Error);
                assert(false);
            }

            // Reset the index for a future run.
            Index = 0;
        }
        void Extractmultiple(const std::function<bool()> &&Callback) noexcept
        {
            // Verify that we didn't forget an argument.
            assert(Argcount == Index);

            auto Result = sqlite3_step(Statement.get());
            while (SQLITE_ROW == Result)
            {
                if (!Callback()) return;
                Result = sqlite3_step(Statement.get());
            }

            // We do a little bit of debugging..
            if (Result != SQLITE_DONE) [[unlikely]]
            {
                const auto Error = sqlite3_errmsg(sqlite3_db_handle(Statement.get()));
                Errorprint(Error);
                assert(false);
            }

            // Reset the index for a future run.
            Index = 0;
        }

        public:
        // Result-extraction operator, lambdas return false to terminate execution or void for full queries.
        template <typename Function> requires (!Value_t<Function>) void operator>>(Function &&Callback) noexcept
        {
            // Interruptible queries.
            Extractmultiple([&Callback, this]()
            {
                return Functionbinder_t<Functiontraits<Function>::argcount>::Run(Statement.get(), Callback);
            });

            sqlite3_reset(Statement.get());
        }
        template <typename... T> void operator>>(std::tuple<T...> &&Value) noexcept
        {
            Extractsingle([&Value, this]()
            {
                Tuple_t<std::tuple<T...>>::iterate(Statement.get(), Value);
            });

            sqlite3_reset(Statement.get());
        }
        template <Value_t T> void operator>>(std::vector<T> &Vector) noexcept
        {
            Extractmultiple([&Vector, this]()
            {
                Vector.emplace_back(getResult<T>(Statement.get(), 0));
            });

            sqlite3_reset(Statement.get());
        }
        template <Value_t T> void operator>>(T &Value) noexcept
        {
            Extractsingle([&Value, this]()
            {
                getResult(Statement.get(), 0, Value);
            });

            sqlite3_reset(Statement.get());
        }

        // Input operator, sequential propagation of the '?' placeholders in the query.
        template <cmp::Byte_t T, size_t N> Statement_t &operator<<(const cmp::Container_t<T, N> &Value) noexcept
        {
            const std::basic_string_view<T> Temp{ Value.begin(), Value.end() };
            return operator<<(Temp);
        }
        template <Value_t T> Statement_t &operator<<(const T &Value) noexcept
        {
            // Ensure a clean state,
            if (Index == 0) [[unlikely]]
            {
                sqlite3_reset(Statement.get());
                sqlite3_clear_bindings(Statement.get());
            }

            bindValue(Statement.get(), ++Index, Value);
            return *this;
        }
        Statement_t &operator<<(std::string_view Value) noexcept
        {
            // Ensure a clean state,
            if (Index == 0) [[unlikely]]
            {
                sqlite3_reset(Statement.get());
                sqlite3_clear_bindings(Statement.get());
            }

            bindValue(Statement.get(), ++Index, Value);
            return *this;
        }

        // Reset can also be used to avoid RTTI evaluation.
        void Execute() noexcept { Extractmultiple([]() {}); sqlite3_reset(Statement.get()); }
        void Reset() const noexcept { sqlite3_reset(Statement.get()); }

        Statement_t(const std::shared_ptr<sqlite3> &Connection, std::string_view SQL) noexcept
        {
            const char *Remaining{};
            sqlite3_stmt *Temp{};

            // For simplicity, we don't accept more than one statement, i.e. only one ';' allowed.
            assert(1 == std::ranges::count(SQL, ';'));
            Argcount = std::ranges::count(SQL, '?');

            // Prepare the statement.
            const auto Result = sqlite3_prepare_v2(Connection.get(), SQL.data(), int(SQL.size()), &Temp, &Remaining);
            if (Result != SQLITE_OK) [[unlikely]]
            {
                const auto Error = sqlite3_errmsg(sqlite3_db_handle(Statement.get()));
                Errorprint(Error);
                assert(false);
            }

            // Save the statement and finalize when we go out of scope.
            Statement = { Temp, sqlite3_finalize };
        }
        Statement_t(const Statement_t &Other) noexcept = default;
        Statement_t(Statement_t &&Other) noexcept = default;
        ~Statement_t() noexcept
        {
            // Need to ensure that the statement was evaluated.
            if (NULL == sqlite3_stmt_status(Statement.get(), SQLITE_STMTSTATUS_RUN, NULL) && Argcount == Index) [[unlikely]]
            {
                Extractmultiple([]() {});
            }
        }
    };
    #pragma pack(pop)

    // Holds the connection and creates the prepared statement(s).
    struct Database_t
    {
        std::shared_ptr<sqlite3> Connection;

        Statement_t operator<<(std::string_view SQL) const noexcept
        {
            return Statement_t(Connection, SQL);
        }
    };

    // Restore warnings.
    #pragma warning(pop)
}
