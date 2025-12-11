#pragma once

#include <utility>

namespace hrz
{
template<typename T>
struct DoubleBuffered
{
private:
    T a;
    T b;

    T* front_ptr;
    T* back_ptr;

public:
    DoubleBuffered() : a({}), b({}), front_ptr(&a), back_ptr(&b) {}

    DoubleBuffered(T&& a_, T&& b_) : a(std::move(a_)), b(std::move(b_)), front_ptr(&a), back_ptr(&b)
    {
    }

    T& front() { return *front_ptr; }

    const T& front() const { return *front_ptr; }

    T& back() { return *back_ptr; }

    const T& back() const { return *back_ptr; }

    void swap() { std::swap(front_ptr, back_ptr); }
};
} // namespace hrz
