/*
    Initial author: Convery (tcn@ayria.se)
    Started: 2022-07-30
    License: MIT

    Fixed capacity container that overwrites the last element as needed.
*/

#pragma once
#include <Utilities/Utilities.hpp>

// Needs std::ranges for sentinel support in algorithms.
template <typename T, size_t N> requires (N > 0)
class Ringbuffer_t
{
    using size_type = cmp::minInt_t<N>;

    size_type Head{}, Size{};
    std::array<T, N> Storage{};

    // For clearer code..
    static constexpr size_type Clamp(size_type Value) noexcept
    {
        return Value % N;
    }
    static constexpr size_type Advance(size_type Index, int Offset) noexcept
    {
        return Clamp(size_type(Index + Offset + N));
    }

    public:
    struct Iterator_t
    {
        using difference_type = std::ptrdiff_t;
        using reference = const T &;
        using pointer = const T *;
        using value_type = T;

        size_type Index{}, Count{};
        const T *Data{};

        reference operator*() const noexcept { return Data[Index]; }
        pointer operator->() const noexcept { return Data + Index; }

        bool operator==(const std::default_sentinel_t &) const noexcept { return Count == 0; }
        bool operator==(const Iterator_t &Right) const noexcept { return Count == Right.Count; }

        Iterator_t &operator++() noexcept { Index = Advance(Index, -1); --Count; return *this; }
        Iterator_t &operator--() noexcept { Index = Advance(Index, 1); ++Count; return *this; }

        Iterator_t operator++(int) noexcept { auto Copy{ *this }; operator++(); return Copy; }
        Iterator_t operator--(int) noexcept { auto Copy{ *this }; operator--(); return Copy; }
    };

    // Not sure if useful for user-code.
    bool empty() const noexcept
    {
        return Size == 0;
    }
    size_type size() const noexcept
    {
        return Size;
    }

    // Simple access to common elements.
    [[nodiscard]] const T &back() const noexcept
    {
        return Storage[Head];
    }
    [[nodiscard]] const T &front() const noexcept
    {
        // On Size == 0, this returns the same value as [0].
        return Storage[Advance(Head, -1)];
    }

    // STL compatibility.
    [[nodiscard]] static auto end() noexcept { return std::default_sentinel_t{}; }
    [[nodiscard]] auto begin() const noexcept { return std::counted_iterator{ Iterator_t{ Advance(Head, -1), Size, Storage.data() }, Size }; }

    // STL-like modifiers.
    void push_back(const T &Value) noexcept
    {
        Storage[Head] = Value;
        if (Size != N) ++Size;

        Head = Advance(Head, 1);
    }
    template <typename... Args> T &emplace_back(Args&&... args) noexcept
    {
        if (Size != N) ++Size;

        Storage[Head] = { std::forward<Args>(args)... };
        auto &Ret = Storage[Head];

        Head = Advance(Head, 1);
        return Ret;
    }
};
