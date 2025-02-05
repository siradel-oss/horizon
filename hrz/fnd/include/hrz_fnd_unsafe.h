#pragma once

namespace hrz
{

/**
 * Used to mark unsafe functions.
 * This forces the caller to explicitly acknowledge that the function is unsafe
 * and explain why the call is valid.
 *
 * Example:
 * float unsafe_sqrt(unsafe, float v);
 *
 * unsafe_sqrt(unsafe("I checked that x is positive"), x);
 */
struct unsafe final
{
    template<typename T>
    explicit constexpr unsafe(T&& /* why */)
    {
    }

private:
    unsafe() = default;
};

} // namespace hrz
