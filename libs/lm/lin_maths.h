#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <utility>

// Navigate with these markers:
// @@VECTOR_TYPES
// @@OPERATIONS
// @@OPERATORS
// @@COMPONENTWISE
// @@VECTOR_ARITHMETIC
// @@FOLD_OPERATORS
// @@VECTOR_OPERATORS
// @@MATRIX_OPERATORS
// @@MATRIX_CONSTRUCT
// @@QUATERNIONS
// @@DUAL_QUATERNIONS
// @@BOUNDINGBOXES

namespace lm
{

template<typename T>
concept Arithmetic = std::is_arithmetic_v<T>;

template<typename From, typename To>
concept ConvertibleToWithoutNarrowing = std::convertible_to<From, To> && std::is_scalar_v<From>
    && std::is_scalar_v<To> && requires(From t) { To{t}; };

template<typename T>
constexpr T sign(T v)
{
    static_assert(std::is_signed_v<T>, "Value type must be signed");
    return (T(0) < v) - (v < T(0));
}

// @@VECTOR_TYPES

template<typename T, int N>
struct Vector;

template<typename T>
struct Vector<T, 2>
{
    union
    {
        T m[2]{};

        struct
        {
            T x, y;
        };

        struct
        {
            T r, g;
        };
    };

    constexpr Vector() = default;

    constexpr explicit Vector(T v) : x{v}, y{v} {}

    constexpr Vector(T x, T y) : x{x}, y{y} {}

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Vector(const Vector<U, 2>& other)
        requires(!std::same_as<T, U>)
        : x{(T)other.x}, y{(T)other.y}
    {
    }
};

template<typename T>
struct Vector<T, 3>
{
    union
    {
        T m[3]{};

        struct
        {
            T x, y, z;
        };

        struct
        {
            T r, g, b;
        };

        struct
        {
            Vector<T, 2> xy;
        };

        struct
        {
            Vector<T, 2> rg;
        };

        struct
        {
            T _x;
            Vector<T, 2> yz;
        };

        struct
        {
            T _r;
            Vector<T, 2> gb;
        };
    };

    constexpr Vector() = default;

    constexpr explicit Vector(T v) : x{v}, y{v}, z{v} {}

    constexpr Vector(T x, T y, T z) : x{x}, y{y}, z{z} {}

    template<std::convertible_to<T> U>
    constexpr Vector(const Vector<U, 2>& v, T z) : x{(T)v.x}, y{(T)v.y}, z{z}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Vector(const Vector<U, 2>& v) : x{(T)v.x}, y{(T)v.y}, z{}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Vector(const Vector<U, 3>& other)
        requires(!std::same_as<T, U>)
        : x{(T)other.x}, y{(T)other.y}, z{(T)other.z}
    {
    }
};

template<typename T>
struct Vector<T, 4>
{
    union
    {
        T m[4]{};

        struct
        {
            T x, y, z, w;
        };

        struct
        {
            T r, g, b, a;
        };

        struct
        {
            Vector<T, 2> xy;
            Vector<T, 2> zw;
        };

        struct
        {
            Vector<T, 2> rg;
            Vector<T, 2> ba;
        };

        struct
        {
            T _x0;
            Vector<T, 2> yz;
        };

        struct
        {
            T _r0;
            Vector<T, 2> gb;
        };

        struct
        {
            Vector<T, 3> xyz;
        };

        struct
        {
            Vector<T, 3> rgb;
        };

        struct
        {
            T _x1;
            Vector<T, 3> yzw;
        };

        struct
        {
            T _r1;
            Vector<T, 3> gba;
        };
    };

    constexpr Vector() = default;

    constexpr explicit Vector(T v) : x{v}, y{v}, z{v}, w{v} {}

    constexpr Vector(T x, T y, T z, T w) : x{x}, y{y}, z{z}, w{w} {}

    template<std::convertible_to<T> U, std::convertible_to<T> V>
    constexpr Vector(const Vector<U, 2>& xy, const Vector<V, 2>& zw) :
        x{(T)xy.x}, y{(T)xy.y}, z{(T)zw.x}, w{(T)zw.y}
    {
    }

    template<std::convertible_to<T> U>
    constexpr Vector(const Vector<U, 2>& v, T z, T w) : x{(T)v.x}, y{(T)v.y}, z{z}, w{w}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Vector(const Vector<U, 2>& v, T z = T{0}) : x{(T)v.x}, y{(T)v.y}, z{z}, w{}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Vector(Vector<U, 3> v) : x{(T)v.x}, y{(T)v.y}, z{(T)v.z}, w{}
    {
    }

    template<std::convertible_to<T> U>
    constexpr Vector(Vector<U, 3> v, T w) : x{(T)v.x}, y{(T)v.y}, z{(T)v.z}, w{w}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Vector(const Vector<U, 4>& other)
        requires(!std::same_as<T, U>)
        : x{(T)other.x}, y{(T)other.y}, z{(T)other.z}, w{(T)other.w}
    {
    }
};

using vec2 = Vector<float, 2>;
using vec3 = Vector<float, 3>;
using vec4 = Vector<float, 4>;

using dvec2 = Vector<double, 2>;
using dvec3 = Vector<double, 3>;
using dvec4 = Vector<double, 4>;

using ivec2 = Vector<int32_t, 2>;
using ivec3 = Vector<int32_t, 3>;
using ivec4 = Vector<int32_t, 4>;

using uvec2 = Vector<uint32_t, 2>;
using uvec3 = Vector<uint32_t, 3>;
using uvec4 = Vector<uint32_t, 4>;

using ubvec2 = Vector<uint8_t, 2>;
using ubvec3 = Vector<uint8_t, 3>;
using ubvec4 = Vector<uint8_t, 4>;

using ibvec2 = Vector<int8_t, 2>;
using ibvec3 = Vector<int8_t, 3>;
using ibvec4 = Vector<int8_t, 4>;

using bvec2 = Vector<bool, 2>;
using bvec3 = Vector<bool, 3>;
using bvec4 = Vector<bool, 4>;

using ilvec2 = Vector<int64_t, 2>;
using ilvec3 = Vector<int64_t, 3>;
using ilvec4 = Vector<int64_t, 4>;

using ulvec2 = Vector<uint64_t, 2>;
using ulvec3 = Vector<uint64_t, 3>;
using ulvec4 = Vector<uint64_t, 4>;

using usvec2 = Vector<uint16_t, 2>;
using usvec3 = Vector<uint16_t, 3>;
using usvec4 = Vector<uint16_t, 4>;

template<typename T, int N>
struct Matrix;

template<typename T>
struct Matrix<T, 2>
{
    union
    {
        T m[2][2]{};
        T e[4];
        Vector<T, 2> col[2];

        struct
        {
            Vector<T, 2> x, y;
        };
    };

    constexpr Matrix() = default;

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Matrix(const Matrix<U, 2>& other)
        requires(!std::same_as<T, U>)
        : x{other.x}, y{other.y}
    {
    }

    // We keep this specifically so we can still write the constructor without specifying
    // lm::Vector<T, ...> for each column.
    constexpr explicit Matrix(const Vector<T, 2>& x, const Vector<T, 2>& y) : x{x}, y{y} {}

    template<std::convertible_to<T> U>
    constexpr explicit Matrix(const Vector<U, 2>& x, const Vector<U, 2>& y)
        requires(!std::same_as<T, U>)
        : x{x}, y{y}
    {
    }

    constexpr explicit Matrix(T v) : x{v}, y{v} {}

    constexpr Matrix(T e0, T e1, T e2, T e3) : e{e0, e1, e2, e3} {}

    static constexpr Matrix<T, 2> identity()
    {
        return Matrix(Vector<T, 2>(1, 0), Vector<T, 2>(0, 1));
    }
};

template<typename T>
struct Matrix<T, 3>
{
    union
    {
        T m[3][3]{};
        T e[9];
        Vector<T, 3> col[3];

        struct
        {
            Vector<T, 3> x, y, z;
        };
    };

    constexpr Matrix() = default;

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Matrix(const Matrix<U, 3>& other)
        requires(!std::same_as<T, U>)
        : x{other.x}, y{other.y}, z{other.z}
    {
    }

    // We keep this specifically so we can still write the constructor without specifying
    // lm::Vector<T, ...> for each column.
    constexpr explicit Matrix(const Vector<T, 3>& x, const Vector<T, 3>& y, const Vector<T, 3>& z) :
        x{x}, y{y}, z{z}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Matrix(const Vector<U, 3>& x, const Vector<U, 3>& y, const Vector<U, 3>& z)
        requires(!std::same_as<T, U>)
        : x{x}, y{y}, z{z}
    {
    }

    template<std::convertible_to<T> U>
    explicit constexpr Matrix(const Matrix<U, 4>& other) :
        x{other.x.xyz}, y{other.y.xyz}, z{other.z.xyz}
    {
    }

    constexpr explicit Matrix(T v) : x{v}, y{v}, z{v} {}

    constexpr Matrix(T e0, T e1, T e2, T e3, T e4, T e5, T e6, T e7, T e8) :
        e{e0, e1, e2, e3, e4, e5, e6, e7, e8}
    {
    }

    static constexpr Matrix<T, 3> identity()
    {
        return Matrix(Vector<T, 3>(1, 0, 0), Vector<T, 3>(0, 1, 0), Vector<T, 3>(0, 0, 1));
    }
};

template<typename T>
struct Matrix<T, 4>
{
    union
    {
        T m[4][4]{};
        T e[16];
        Vector<T, 4> col[4];

        struct
        {
            Vector<T, 4> x, y, z, w;
        };
    };

    constexpr Matrix() = default;

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Matrix(const Matrix<U, 4>& other)
        requires(!std::same_as<T, U>)
        : x{other.x}, y{other.y}, z{other.z}, w{other.w}
    {
    }

    // We keep this specifically so we can still write the constructor without specifying
    // lm::Vector<T, ...> for each column.
    constexpr explicit Matrix(
        const Vector<T, 4>& x,
        const Vector<T, 4>& y,
        const Vector<T, 4>& z,
        const Vector<T, 4>& w) :
        x{x}, y{y}, z{z}, w{w}
    {
    }

    template<std::convertible_to<T> U>
    explicit constexpr Matrix(
        const Vector<U, 4>& x,
        const Vector<U, 4>& y,
        const Vector<U, 4>& z,
        const Vector<U, 4>& w)
        requires(!std::same_as<T, U>)
        : x{x}, y{y}, z{z}, w{w}
    {
    }

    constexpr explicit Matrix(T v) : x{v}, y{v}, z{v}, w{v} {}

    constexpr Matrix(
        T e0,
        T e1,
        T e2,
        T e3,
        T e4,
        T e5,
        T e6,
        T e7,
        T e8,
        T e9,
        T e10,
        T e11,
        T e12,
        T e13,
        T e14,
        T e15) :
        e{e0, e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15}
    {
    }

    static constexpr Matrix<T, 4> identity()
    {
        return Matrix(
            Vector<T, 4>(1, 0, 0, 0), Vector<T, 4>(0, 1, 0, 0), Vector<T, 4>(0, 0, 1, 0),
            Vector<T, 4>(0, 0, 0, 1));
    }
};

using mat2 = Matrix<float, 2>;
using mat3 = Matrix<float, 3>;
using mat4 = Matrix<float, 4>;

using dmat2 = Matrix<double, 2>;
using dmat3 = Matrix<double, 3>;
using dmat4 = Matrix<double, 4>;

// @@OPERATIONS

template<typename F, typename... T>
concept Operator =
    requires { typename F::ResultType; } && std::semiregular<F> && std::invocable<F, T...>
    && std::same_as<std::invoke_result_t<F, T...>, typename F::ResultType>;

template<typename F, typename T>
concept FoldOperator = Operator<F, T, T> && std::same_as<T, typename F::ResultType>;

// @Todo(C++23) Use static operator() for all those operations. Maybe some won't need to be a
// functor anymore.

template<typename U, typename V = U>
struct AddOp
{
    using ResultType = decltype(std::declval<U>() + std::declval<V>());

    constexpr ResultType operator()(U a, V b) const { return (ResultType)a + (ResultType)b; }
};

template<typename U, typename V = U>
struct SubOp
{
    using ResultType = decltype(std::declval<U>() - std::declval<V>());

    constexpr ResultType operator()(U a, V b) const { return (ResultType)a - (ResultType)b; }
};

template<typename U, typename V = U>
struct MulOp
{
    using ResultType = decltype(std::declval<U>() * std::declval<V>());

    constexpr ResultType operator()(U a, V b) const { return (ResultType)a * (ResultType)b; }
};

template<typename U, typename V = U>
struct DivOp
{
    using ResultType = decltype(std::declval<U>() / std::declval<V>());

    constexpr ResultType operator()(U a, V b) const { return (ResultType)a / (ResultType)b; }
};

template<typename T>
struct NegOp
{
    using ResultType = T;

    constexpr ResultType operator()(T x) const { return -x; }
};

template<typename U, typename V = U>
struct EqualOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a == b; }
};

template<typename U, typename V = U>
struct NotEqualOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a != b; }
};

template<typename U, typename V = U>
struct LEqualOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a <= b; }
};

template<typename U, typename V = U>
struct GEqualOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a >= b; }
};

template<typename U, typename V = U>
struct LessOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a < b; }
};

template<typename U, typename V = U>
struct GreaterOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a > b; }
};

template<typename U, typename V = U>
struct OrOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a || b; }
};

template<typename U, typename V = U>
struct AndOp
{
    using ResultType = bool;

    constexpr ResultType operator()(U a, V b) const { return a && b; }
};

template<typename T>
struct NotOp
{
    using ResultType = bool;

    constexpr ResultType operator()(T x) const { return !x; }
};

template<typename T>
struct SignOp
{
    using ResultType = T;

    constexpr ResultType operator()(T x) const { return lm::sign(x); }
};

#define IMPL_UNARY_SIMPLE(NAME, FN)                \
    template<typename T>                           \
    struct NAME                                    \
    {                                              \
        using ResultType = T;                      \
        constexpr ResultType operator()(T x) const \
        {                                          \
            return (T)std::FN(x);                  \
        }                                          \
    };
IMPL_UNARY_SIMPLE(AbsOp, abs)
IMPL_UNARY_SIMPLE(CeilOp, ceil)
IMPL_UNARY_SIMPLE(FloorOp, floor)
IMPL_UNARY_SIMPLE(RoundOp, round)
IMPL_UNARY_SIMPLE(ExpOp, exp)
IMPL_UNARY_SIMPLE(LogOp, log)
IMPL_UNARY_SIMPLE(Log10Op, log10)
IMPL_UNARY_SIMPLE(SqrtOp, sqrt)
IMPL_UNARY_SIMPLE(SinOp, sin)
IMPL_UNARY_SIMPLE(CosOp, cos)
IMPL_UNARY_SIMPLE(TanOp, tan)
IMPL_UNARY_SIMPLE(AsinOp, asin)
IMPL_UNARY_SIMPLE(AcosOp, acos)
IMPL_UNARY_SIMPLE(AtanOp, atan)
#undef IMPL_UNARY_SIMPLE

#define IMPL_BINARY_SIMPLE(NAME, FN)                    \
    template<typename T>                                \
    struct NAME                                         \
    {                                                   \
        using ResultType = T;                           \
        constexpr ResultType operator()(T x, T y) const \
        {                                               \
            return (T)std::FN(x, y);                    \
        }                                               \
    };
IMPL_BINARY_SIMPLE(FmodOp, fmod)
IMPL_BINARY_SIMPLE(PowOp, pow)
IMPL_BINARY_SIMPLE(Atan2Op, atan2)
#undef IMPL_BINARY_SIMPLE

template<typename T>
struct MinOp
{
    using ResultType = T;

    constexpr ResultType operator()(T x, T y) const { return (x > y) ? y : x; }
};

template<typename T>
struct MaxOp
{
    using ResultType = T;

    constexpr ResultType operator()(T x, T y) const { return (x < y) ? y : x; }
};

template<typename T>
struct ClampOp
{
    using ResultType = T;

    constexpr ResultType operator()(T x, T min_v, T max_v) const
    {
        return (x < min_v) ? min_v : ((x > max_v) ? max_v : x);
    }
};

// @@OPERATORS

// We would like to use T... but we can't control how multiple parameter packs are expanded at once.
// So we specialize this for 1, 2, and 3 inputs.

template<typename T, int N, int... Ns>
constexpr auto apply(std::integer_sequence<int, Ns...>, Operator<T> auto f, const Vector<T, N>& v)
{
    return Vector<typename decltype(f)::ResultType, N>{f(v.m[Ns])...};
}

template<typename U, typename V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    const Vector<U, N>& u,
    const Vector<V, N>& v)
{
    return Vector<typename decltype(f)::ResultType, N>{f(u.m[Ns], v.m[Ns])...};
}

template<typename U, typename V, typename W, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V, W> auto f,
    const Vector<U, N>& u,
    const Vector<V, N>& v,
    const Vector<W, N>& w)
{
    return Vector<typename decltype(f)::ResultType, N>{f(u.m[Ns], v.m[Ns], w.m[Ns])...};
}

template<typename U, typename V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    const Matrix<U, N>& mu,
    const Matrix<V, N>& mv)
{
    return Matrix<typename decltype(f)::ResultType, N>{f(mu.e[Ns], mv.e[Ns])...};
}

// Optimize vector-scalar and scalar-vector operations because they are fairly common
// and this avoids having to create a temporary vector of the scalar value.
// Same for matrices.

template<typename U, Arithmetic V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    const Vector<U, N>& u,
    V v)
{
    return Vector<typename decltype(f)::ResultType, N>{f(u.m[Ns], v)...};
}

template<Arithmetic U, typename V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    U u,
    const Vector<V, N>& v)
{
    return Vector<typename decltype(f)::ResultType, N>{f(u, v.m[Ns])...};
}

template<typename U, Arithmetic V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    const Matrix<U, N>& u,
    V v)
{
    return Matrix<typename decltype(f)::ResultType, N>{f(u.e[Ns], v)...};
}

template<Arithmetic U, typename V, int N, int... Ns>
constexpr auto apply(
    std::integer_sequence<int, Ns...>,
    Operator<U, V> auto f,
    U u,
    const Matrix<V, N>& v)
{
    return Matrix<typename decltype(f)::ResultType, N>{f(u, v.e[Ns])...};
}

template<typename T, int N, int... Ns>
constexpr auto fold(
    std::integer_sequence<int, Ns...>,
    FoldOperator<T> auto f,
    const Vector<T, N>& v,
    T result)
{
    ((result = f(result, v.m[Ns])), ...);
    return result;
}

template<typename T, int N, int... Ns>
constexpr auto fold(
    std::integer_sequence<int, Ns...>,
    FoldOperator<T> auto f,
    const Matrix<T, N>& v,
    T result)
{
    ((result = f(result, v.e[Ns])), ...);
    return result;
}

// @@COMPONENTWISE

#define IMPL_VEC_BINARY_OP_WITH_SCALAR(OP, NAME)                              \
    template<typename U, typename V, int N>                                   \
    constexpr auto NAME(const Vector<U, N>& a, const Vector<V, N>& b)         \
    {                                                                         \
        return apply(std::make_integer_sequence<int, N>{}, OP<U, V>{}, a, b); \
    }                                                                         \
    template<typename U, Arithmetic V, int N>                                 \
    constexpr auto NAME(const Vector<U, N>& a, V b)                           \
    {                                                                         \
        return apply(std::make_integer_sequence<int, N>{}, OP<U, V>{}, a, b); \
    }                                                                         \
    template<Arithmetic U, typename V, int N>                                 \
    constexpr auto NAME(U a, const Vector<V, N>& b)                           \
    {                                                                         \
        return apply(std::make_integer_sequence<int, N>{}, OP<U, V>{}, a, b); \
    }
IMPL_VEC_BINARY_OP_WITH_SCALAR(AddOp, operator+)
IMPL_VEC_BINARY_OP_WITH_SCALAR(SubOp, operator-)
IMPL_VEC_BINARY_OP_WITH_SCALAR(MulOp, operator*)
IMPL_VEC_BINARY_OP_WITH_SCALAR(DivOp, operator/)
IMPL_VEC_BINARY_OP_WITH_SCALAR(EqualOp, eq)
IMPL_VEC_BINARY_OP_WITH_SCALAR(NotEqualOp, neq)
IMPL_VEC_BINARY_OP_WITH_SCALAR(LessOp, operator<)
IMPL_VEC_BINARY_OP_WITH_SCALAR(GreaterOp, operator>)
IMPL_VEC_BINARY_OP_WITH_SCALAR(LEqualOp, operator<=)
IMPL_VEC_BINARY_OP_WITH_SCALAR(GEqualOp, operator>=)
IMPL_VEC_BINARY_OP_WITH_SCALAR(AndOp, operator&&)
IMPL_VEC_BINARY_OP_WITH_SCALAR(OrOp, operator||)
#undef IMPL_VEC_BINARY_OP_WITH_SCALAR

template<typename T, int N>
constexpr Vector<T, N>& operator+=(Vector<T, N>& a, const Vector<T, N>& b)
{
    a = apply(std::make_integer_sequence<int, N>{}, AddOp<T>{}, a, b);
    return a;
}

template<typename T, int N>
constexpr Vector<T, N>& operator-=(Vector<T, N>& a, const Vector<T, N>& b)
{
    a = apply(std::make_integer_sequence<int, N>{}, SubOp<T>{}, a, b);
    return a;
}

template<typename T, int N>
constexpr Vector<T, N>& operator*=(Vector<T, N>& a, const Vector<T, N>& v)
{
    a = apply(std::make_integer_sequence<int, N>{}, MulOp<T>{}, a, v);
    return a;
}

template<typename T, int N>
constexpr Vector<T, N>& operator*=(Vector<T, N>& a, T v)
{
    a = apply(std::make_integer_sequence<int, N>{}, MulOp<T>{}, a, v);
    return a;
}

template<typename T, int N>
constexpr Vector<T, N>& operator/=(Vector<T, N>& a, const Vector<T, N>& v)
{
    a = apply(std::make_integer_sequence<int, N>{}, DivOp<T>{}, a, v);
    return a;
}

template<typename T, int N>
constexpr Vector<T, N>& operator/=(Vector<T, N>& a, T v)
{
    a = apply(std::make_integer_sequence<int, N>{}, DivOp<T>{}, a, v);
    return a;
}

template<typename U, Arithmetic V, int N>
constexpr auto operator*(const Matrix<U, N>& a, V b)
{
    return apply(std::make_integer_sequence<int, N * N>{}, MulOp<U, V>{}, a, b);
}

template<Arithmetic U, typename V, int N>
constexpr auto operator*(U a, const Matrix<V, N>& b)
{
    return apply(std::make_integer_sequence<int, N * N>{}, MulOp<U, V>{}, a, b);
}

template<typename T, int N>
constexpr Matrix<T, N>& operator*=(Matrix<T, N>& a, T b)
{
    a = apply(std::make_integer_sequence<int, N * N>{}, MulOp<T>{}, a, b);
    return a;
}

template<typename U, Arithmetic V, int N>
constexpr auto operator/(const Matrix<U, N>& a, V b)
{
    return apply(std::make_integer_sequence<int, N * N>{}, DivOp<U, V>{}, a, b);
}

template<Arithmetic U, typename V, int N>
constexpr auto operator/(U a, const Matrix<V, N>& b)
{
    return apply(std::make_integer_sequence<int, N * N>{}, DivOp<U, V>{}, a, b);
}

template<typename T, int N>
constexpr Matrix<T, N>& operator/=(Matrix<T, N>& a, T b)
{
    a = apply(std::make_integer_sequence<int, N * N>{}, DivOp<T>{}, a, b);
    return a;
}

#define IMPL_VEC_UNARY_OP(OP, NAME)                                     \
    template<typename T, int N>                                         \
    constexpr auto NAME(const Vector<T, N>& v)                          \
    {                                                                   \
        return apply(std::make_integer_sequence<int, N>{}, OP<T>{}, v); \
    }
IMPL_VEC_UNARY_OP(NegOp, operator-)
IMPL_VEC_UNARY_OP(NotOp, operator!)
IMPL_VEC_UNARY_OP(SignOp, sign)
IMPL_VEC_UNARY_OP(AbsOp, abs)
IMPL_VEC_UNARY_OP(CeilOp, ceil)
IMPL_VEC_UNARY_OP(FloorOp, floor)
IMPL_VEC_UNARY_OP(RoundOp, round)
IMPL_VEC_UNARY_OP(ExpOp, exp)
IMPL_VEC_UNARY_OP(LogOp, log)
IMPL_VEC_UNARY_OP(Log10Op, log10)
IMPL_VEC_UNARY_OP(SqrtOp, sqrt)
IMPL_VEC_UNARY_OP(SinOp, sin)
IMPL_VEC_UNARY_OP(CosOp, cos)
IMPL_VEC_UNARY_OP(TanOp, tan)
IMPL_VEC_UNARY_OP(AsinOp, asin)
IMPL_VEC_UNARY_OP(AcosOp, acos)
IMPL_VEC_UNARY_OP(AtanOp, atan)
#undef IMPL_VEC_UNARY_OP

#define IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(OP, NAME)                 \
    template<typename T, int N>                                            \
    constexpr auto NAME(const Vector<T, N>& a, const Vector<T, N>& b)      \
    {                                                                      \
        return apply(std::make_integer_sequence<int, N>{}, OP<T>{}, a, b); \
    }                                                                      \
    template<typename T, int N>                                            \
    constexpr auto NAME(const Vector<T, N>& a, T b)                        \
    {                                                                      \
        return apply(std::make_integer_sequence<int, N>{}, OP<T>{}, a, b); \
    }                                                                      \
    template<typename T, int N>                                            \
    constexpr auto NAME(T a, const Vector<T, N>& b)                        \
    {                                                                      \
        return apply(std::make_integer_sequence<int, N>{}, OP<T>{}, a, b); \
    }
IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(FmodOp, fmod)
IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(PowOp, pow)
IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(Atan2Op, atan2)
IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(MinOp, min)
IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE(MaxOp, max)
#undef IMPL_VEC_BINARY_OP_WITH_SCALAR_SAME_TYPE

#define IMPL_VEC_TERNARY_OP(OP, NAME)                                              \
    template<typename T, int N>                                                    \
    auto NAME(const Vector<T, N>& a, const Vector<T, N>& b, const Vector<T, N>& c) \
    {                                                                              \
        return apply(std::make_integer_sequence<int, N>{}, OP<T>{}, a, b, c);      \
    }
IMPL_VEC_TERNARY_OP(ClampOp, clamp)
#undef IMPL_VEC_TERNARY_OP

template<typename T, int N>
constexpr Vector<T, N> mix(const Vector<T, N>& x, const Vector<T, N>& y, const Vector<T, N>& a)
{
    return x * (Vector<T, N>(1) - a) + y * a;
}

template<typename T, int N>
constexpr Vector<T, N> mix(const Vector<T, N>& x, const Vector<T, N>& y, T a)
{
    return x * ((T)1 - a) + y * a;
}

// @@VECTOR_ARITHMETIC

template<typename U, typename V, int N>
constexpr auto operator*(const Matrix<U, N>& a, const Vector<V, N>& b)
{
    return [&a, &b]<int... Ns>(std::integer_sequence<int, Ns...>) {
        return Vector<typename MulOp<U, V>::ResultType, N>{((a.col[Ns] * b.m[Ns]) + ...)};
    }(std::make_integer_sequence<int, N>{});
}

template<typename U, typename V, int N>
constexpr auto operator*(const Matrix<U, N>& a, const Matrix<V, N>& b)
{
    return [&a, &b]<int... Ns>(std::integer_sequence<int, Ns...>) {
        return Matrix<typename MulOp<U, V>::ResultType, N>{a * b.col[Ns]...};
    }(std::make_integer_sequence<int, N>{});
}

// @@FOLD_OPERATORS

template<int N>
constexpr bool any(const Vector<bool, N>& v)
{
    return fold(std::make_integer_sequence<int, N>{}, OrOp<bool>{}, v, false);
}

template<int N>
constexpr bool all(const Vector<bool, N>& v)
{
    return fold(std::make_integer_sequence<int, N>{}, AndOp<bool>{}, v, true);
}

template<typename T, int N>
constexpr T minelem(const Vector<T, N>& v)
{
    return fold(std::make_integer_sequence<int, N>{}, MinOp<T>{}, v, v.m[0]);
}

template<typename T, int N>
constexpr T maxelem(const Vector<T, N>& v)
{
    return fold(std::make_integer_sequence<int, N>{}, MaxOp<T>{}, v, v.m[0]);
}

template<typename T, int N>
constexpr T sum(const Vector<T, N>& v)
{
    return fold(std::make_integer_sequence<int, N>{}, AddOp<T>{}, v, T{0});
}

// @@VECTOR_OPERATORS

template<typename U, typename V, int N>
constexpr bool operator==(const Vector<U, N>& a, const Vector<V, N>& b)
{
    return [&a, &b]<int... Ns>(std::integer_sequence<int, Ns...>)
    { return ((a.m[Ns] == b.m[Ns]) && ...); }(std::make_integer_sequence<int, N>{});
}

template<typename U, typename V, int N, typename R = typename MulOp<U, V>::ResultType>
constexpr R dot(const Vector<U, N>& a, const Vector<V, N>& b)
{
    return sum(a * b);
}

template<typename T, int N>
T length2(const Vector<T, N>& v)
{
    return dot(v, v);
}

template<typename T, int N>
T length(const Vector<T, N>& v)
{
    return std::sqrt(length2(v));
}

template<typename T, int N>
Vector<T, N> normalize(const Vector<T, N>& v)
{
    T inv_length = (T)1 / length(v);
    return v * inv_length;
}

template<typename U, typename V>
auto cross(const Vector<U, 3>& a, const Vector<V, 3>& b)
{
    return Vector<typename MulOp<U, V>::ResultType, 3>{
        a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

template<typename T, int N>
bool has_nan(const Vector<T, N>& v)
{
    return [&v]<int... Ns>(std::integer_sequence<int, Ns...>)
    { return ((std::isnan(v.m[Ns]) || ...)); }(std::make_integer_sequence<int, N>{});
}

// @@MATRIX_OPERATORS

template<typename T, int N>
Matrix<T, N> transpose(const Matrix<T, N>& m)
{
    Matrix<T, N> t(m);
    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < i; ++j)
        {
            T tmp = t[i][j];
            t[i][j] = t[j][i];
            t[j][i] = tmp;
        }
    }
    return t;
}

template<typename T>
constexpr Matrix<T, 2> transpose(const Matrix<T, 2>& m)
{
    return Matrix<T, 2>{Vector<T, 2>{m.x.x, m.y.x}, Vector<T, 2>{m.x.y, m.y.y}};
}

template<typename T>
constexpr Matrix<T, 3> transpose(const Matrix<T, 3>& m)
{
    return Matrix<T, 3>{
        Vector<T, 3>{m.x.x, m.y.x, m.z.x},
        Vector<T, 3>{m.x.y, m.y.y, m.z.y},
        Vector<T, 3>{m.x.z, m.y.z, m.z.z},
    };
}

template<typename T>
constexpr Matrix<T, 4> transpose(const Matrix<T, 4>& m)
{
    return Matrix<T, 4>{
        Vector<T, 4>{m.x.x, m.y.x, m.z.x, m.w.x},
        Vector<T, 4>{m.x.y, m.y.y, m.z.y, m.w.y},
        Vector<T, 4>{m.x.z, m.y.z, m.z.z, m.w.z},
        Vector<T, 4>{m.x.w, m.y.w, m.z.w, m.w.w},
    };
}

template<typename T>
constexpr T determinant(const Matrix<T, 2>& m)
{
    return m.x.x * m.y.y - m.x.y * m.y.x;
}

template<typename T>
constexpr T determinant(const Matrix<T, 3>& m)
{
    return m.x.x * (m.y.y * m.z.z - m.y.z * m.z.y) - m.y.x * (m.x.y * m.z.z - m.x.z * m.z.y)
        + m.z.x * (m.x.y * m.y.z - m.x.z * m.y.y);
}

template<typename T>
constexpr T determinant(const Matrix<T, 4>& m)
{
    return m.x.x
        * (m.y.y * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.y * (m.y.z * m.w.w - m.y.w * m.w.z)
           + m.w.y * (m.y.z * m.z.w - m.y.w * m.z.z))
        - m.y.x
        * (m.x.y * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.y * (m.x.z * m.w.w - m.x.w * m.w.z)
           + m.w.y * (m.x.z * m.z.w - m.x.w * m.z.z))
        + m.z.x
        * (m.x.y * (m.y.z * m.w.w - m.y.w * m.w.z) - m.y.y * (m.x.z * m.w.w - m.x.w * m.w.z)
           + m.w.y * (m.x.z * m.y.w - m.x.w * m.y.z))
        - m.w.x
        * (m.x.y * (m.y.z * m.z.w - m.y.w * m.z.z) - m.y.y * (m.x.z * m.z.w - m.x.w * m.z.z)
           + m.z.y * (m.x.z * m.y.w - m.x.w * m.y.z));
}

template<typename T>
constexpr Matrix<T, 2> adjugate(const Matrix<T, 2>& m)
{
    return Matrix<T, 2>(Vector<T, 2>{m.y.y, -m.x.y}, Vector<T, 2>{-m.y.x, m.x.x});
}

template<typename T>
constexpr Matrix<T, 3> adjugate(const Matrix<T, 3>& m)
{
    return Matrix<T, 3>(
        Vector<T, 3>{
            m.y.y * m.z.z - m.y.z * m.z.y, m.x.z * m.z.y - m.x.y * m.z.z,
            m.x.y * m.y.z - m.x.z * m.y.y},
        Vector<T, 3>{
            m.y.z * m.z.x - m.y.x * m.z.z, m.x.x * m.z.z - m.x.z * m.z.x,
            m.x.z * m.y.x - m.x.x * m.y.z},
        Vector<T, 3>{
            m.y.x * m.z.y - m.y.y * m.z.x, m.x.y * m.z.x - m.x.x * m.z.y,
            m.x.x * m.y.y - m.x.y * m.y.x});
}

template<typename T>
constexpr Matrix<T, 4> adjugate(const Matrix<T, 4>& m)
{
    return Matrix<T, 4>(
        Vector<T, 4>{
            m.y.y * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.y * (m.y.z * m.w.w - m.y.w * m.w.z)
                + m.w.y * (m.y.z * m.z.w - m.y.w * m.z.z),
            -(m.x.y * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.y * (m.x.z * m.w.w - m.x.w * m.w.z)
              + m.w.y * (m.x.z * m.z.w - m.x.w * m.z.z)),
            m.x.y * (m.y.z * m.w.w - m.y.w * m.w.z) - m.y.y * (m.x.z * m.w.w - m.x.w * m.w.z)
                + m.w.y * (m.x.z * m.y.w - m.x.w * m.y.z),
            -(m.x.y * (m.y.z * m.z.w - m.y.w * m.z.z) - m.y.y * (m.x.z * m.z.w - m.x.w * m.z.z)
              + m.z.y * (m.x.z * m.y.w - m.x.w * m.y.z))},
        Vector<T, 4>{
            -(m.y.x * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.x * (m.y.z * m.w.w - m.y.w * m.w.z)
              + m.w.x * (m.y.z * m.z.w - m.y.w * m.z.z)),
            m.x.x * (m.z.z * m.w.w - m.z.w * m.w.z) - m.z.x * (m.x.z * m.w.w - m.x.w * m.w.z)
                + m.w.x * (m.x.z * m.z.w - m.x.w * m.z.z),
            -(m.x.x * (m.y.z * m.w.w - m.y.w * m.w.z) - m.y.x * (m.x.z * m.w.w - m.x.w * m.w.z)
              + m.w.x * (m.x.z * m.y.w - m.x.w * m.y.z)),
            m.x.x * (m.y.z * m.z.w - m.y.w * m.z.z) - m.y.x * (m.x.z * m.z.w - m.x.w * m.z.z)
                + m.z.x * (m.x.z * m.y.w - m.x.w * m.y.z)},
        Vector<T, 4>{
            m.y.x * (m.z.y * m.w.w - m.z.w * m.w.y) - m.z.x * (m.y.y * m.w.w - m.y.w * m.w.y)
                + m.w.x * (m.y.y * m.z.w - m.y.w * m.z.y),
            -(m.x.x * (m.z.y * m.w.w - m.z.w * m.w.y) - m.z.x * (m.x.y * m.w.w - m.x.w * m.w.y)
              + m.w.x * (m.x.y * m.z.w - m.x.w * m.z.y)),
            m.x.x * (m.y.y * m.w.w - m.y.w * m.w.y) - m.y.x * (m.x.y * m.w.w - m.x.w * m.w.y)
                + m.w.x * (m.x.y * m.y.w - m.x.w * m.y.y),
            -(m.x.x * (m.y.y * m.z.w - m.y.w * m.z.y) - m.y.x * (m.x.y * m.z.w - m.x.w * m.z.y)
              + m.z.x * (m.x.y * m.y.w - m.x.w * m.y.y))},
        Vector<T, 4>{
            -(m.y.x * (m.z.y * m.w.z - m.z.z * m.w.y) - m.z.x * (m.y.y * m.w.z - m.y.z * m.w.y)
              + m.w.x * (m.y.y * m.z.z - m.y.z * m.z.y)),
            m.x.x * (m.z.y * m.w.z - m.z.z * m.w.y) - m.z.x * (m.x.y * m.w.z - m.x.z * m.w.y)
                + m.w.x * (m.x.y * m.z.z - m.x.z * m.z.y),
            -(m.x.x * (m.y.y * m.w.z - m.y.z * m.w.y) - m.y.x * (m.x.y * m.w.z - m.x.z * m.w.y)
              + m.w.x * (m.x.y * m.y.z - m.x.z * m.y.y)),
            m.x.x * (m.y.y * m.z.z - m.y.z * m.z.y) - m.y.x * (m.x.y * m.z.z - m.x.z * m.z.y)
                + m.z.x * (m.x.y * m.y.z - m.x.z * m.y.y)});
}

template<typename T, int N>
Matrix<T, N> inverse(const Matrix<T, N>& m)
{
    T inv_det = (T)1 / determinant(m);
    return lm::adjugate(m) * inv_det;
}

template<typename U, typename V, int N>
constexpr bool operator==(const Matrix<U, N>& a, const Matrix<V, N>& b)
{
    return [&a, &b]<int... Ns>(std::integer_sequence<int, Ns...>)
    { return ((a.e[Ns] == b.e[Ns]) && ...); }(std::make_integer_sequence<int, N * N>{});
}

// @@MATRIX_CONSTRUCT

static constexpr double PI = 3.14159265358979323846;
static constexpr float PIf = 3.14159265358979323846f;
static constexpr float TWO_PIf = 6.2831853071795864769252867666f;
static constexpr double SQRT2 = 1.4142135623730950488;
static constexpr float SQRT2f = 1.4142135623730950488f;

template<typename T>
constexpr T degrees(T radians)
{
    return radians * (T)(180.0 / PI);
}

template<typename T>
constexpr T radians(T degrees)
{
    return degrees * (T)(PI / 180.0);
}

template<typename T>
Matrix<T, 4> rotation(const Vector<T, 3>& a, T angle)
{
    T c = std::cos(angle);
    T s = std::sin(angle);

    return Matrix<T, 4>(
        Vector<T, 4>{
            c + a.x * a.x * (1 - c), a.y * a.x * (1 - c) + a.z * s, a.z * a.x * (1 - c) - a.y * s,
            0},
        Vector<T, 4>{
            a.x * a.y * (1 - c) - a.z * s, c + a.y * a.y * (1 - c), a.z * a.y * (1 - c) + a.x * s,
            0},
        Vector<T, 4>{
            a.x * a.z * (1 - c) + a.y * s,
            a.y * a.z * (1 - c) - a.x * s,
            c + a.z * a.z * (1 - c),
            0,
        },
        Vector<T, 4>{0, 0, 0, 1});
}

template<typename T>
constexpr Matrix<T, 4> translation(const Vector<T, 3>& p)
{
    return Matrix<T, 4>(
        Vector<T, 4>(1, 0, 0, 0), Vector<T, 4>(0, 1, 0, 0), Vector<T, 4>(0, 0, 1, 0),
        Vector<T, 4>(p.x, p.y, p.z, 1));
}

template<typename T>
constexpr Matrix<T, 4> scaling(const Vector<T, 3>& s)
{
    return Matrix<T, 4>(
        Vector<T, 4>(s.x, 0, 0, 0), Vector<T, 4>(0, s.y, 0, 0), Vector<T, 4>(0, 0, s.z, 0),
        Vector<T, 4>(0, 0, 0, 1));
}

template<typename T>
constexpr Matrix<T, 4> scaling(const T& s)
{
    return Matrix<T, 4>(
        Vector<T, 4>(s, 0, 0, 0), Vector<T, 4>(0, s, 0, 0), Vector<T, 4>(0, 0, s, 0),
        Vector<T, 4>(0, 0, 0, 1));
}

template<typename T>
Matrix<T, 4> orthographic_opengl(T left, T right, T bottom, T top, T near, T far)
{
    return Matrix<T, 4>(
        Vector<T, 4>((T)2 / (right - left), 0, 0, 0), Vector<T, 4>(0, (T)2 / (top - bottom), 0, 0),
        Vector<T, 4>(0, 0, -(T)2 / (far - near), 0),
        Vector<T, 4>(
            -(right + left) / (right - left), -(top + bottom) / (top - bottom),
            -(far + near) / (far - near), 1));
}

template<typename T>
inline Matrix<T, 4> orthographic_opengl(T width, T height, T near, T far)
{
    return orthographic_opengl(-width / 2, width / 2, -height / 2, height / 2, near, far);
}

template<typename T>
Matrix<T, 4> perspective_opengl(T left, T right, T bottom, T top, T near, T far)
{
    return Matrix<T, 4>(
        Vector<T, 4>((T)2 * near / (right - left), 0, 0, 0),
        Vector<T, 4>(0, (T)2 * near / (top - bottom), 0, 0),
        Vector<T, 4>(
            (right + left) / (right - left), (top + bottom) / (top - bottom),
            (near + far) / (near - far), -1),
        Vector<T, 4>(0, 0, ((T)2 * far * near) / (near - far), 0));
}

template<typename T>
inline Matrix<T, 4> perspective_subfrustum_opengl(
    T fovy,
    T aspect,
    T left_norm,
    T right_norm,
    T bottom_norm,
    T top_norm,
    T near,
    T far)
{
    T y_half_extent = near * std::tan(fovy / 2);
    T x_half_extent = y_half_extent * aspect;
    return perspective_opengl(
        left_norm * x_half_extent, right_norm * x_half_extent, bottom_norm * y_half_extent,
        top_norm * y_half_extent, near, far);
}

template<typename T>
inline Matrix<T, 4> perspective_opengl(T fovy, T aspect, T near, T far)
{
    return perspective_subfrustum_opengl(fovy, aspect, (T)-1, (T)1, (T)-1, (T)1, near, far);
}

template<typename T>
Matrix<T, 4> view(const Vector<T, 3>& eye, const Vector<T, 3>& target, const Vector<T, 3>& up)
{
    Vector<T, 3> direction = normalize(target - eye);
    Vector<T, 3> x = normalize(cross(direction, up));
    Vector<T, 3> y = normalize(cross(x, direction));

    Matrix<T, 4> rotation(
        Vector<T, 4>(x.x, y.x, -direction.x, 0), Vector<T, 4>(x.y, y.y, -direction.y, 0),
        Vector<T, 4>(x.z, y.z, -direction.z, 0), Vector<T, 4>(0, 0, 0, 1));

    return rotation * translation(-eye);
}

// @@QUATERNIONS

template<typename T>
struct Quaternion
{
    union
    {
        struct
        {
            T x, y, z, w;
        };

        Vector<T, 3> xyz;
        T m[4];
    };

    constexpr Quaternion() : x{0}, y{0}, z{0}, w{1} {}

    template<std::convertible_to<T> U>
    constexpr explicit Quaternion(U x, U y, U z, U w) : x{(T)x}, y{(T)y}, z{(T)z}, w{(T)w}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Quaternion(const Vector<U, 3>& a, U w) :
        x{(T)a.x}, y{(T)a.y}, z{(T)a.z}, w{(T)w}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Quaternion(const Quaternion<U>& q)
        requires(!std::is_same_v<U, T>)
        : x{(T)q.x}, y{(T)q.y}, z{(T)q.z}, w{(T)q.w}
    {
    }
};

using quat = Quaternion<float>;
using dquat = Quaternion<double>;

template<typename T>
constexpr Quaternion<T> operator-(const Quaternion<T>& q)
{
    return Quaternion<T>(-q.x, -q.y, -q.z, -q.w);
}

template<typename T>
constexpr bool operator==(const Quaternion<T>& a, const Quaternion<T>& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

template<typename T>
constexpr Quaternion<T> conjugate(const Quaternion<T>& q)
{
    return Quaternion<T>(-q.x, -q.y, -q.z, q.w);
}

template<typename U, typename V, typename R = typename MulOp<U, V>::ResultType>
constexpr Quaternion<R> operator*(const Quaternion<U>& a, const Quaternion<V>& b)
{
    return Quaternion<R>(
        (R)(a.x * b.w + a.y * b.z - a.z * b.y + a.w * b.x),
        (R)(-a.x * b.z + a.y * b.w + a.z * b.x + a.w * b.y),
        (R)(a.x * b.y - a.y * b.x + a.z * b.w + a.w * b.z),
        (R)(-a.x * b.x - a.y * b.y - a.z * b.z + a.w * b.w));
}

template<typename U, typename V, typename R = typename AddOp<U, V>::ResultType>
constexpr Quaternion<R> operator+(const Quaternion<U>& a, const Quaternion<V>& b)
{
    return Quaternion<R>(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

template<typename T>
constexpr Quaternion<T>& operator*=(Quaternion<T>& a, const Quaternion<T>& b)
{
    a = a * b;
    return a;
}

template<typename U, typename V, typename R = typename MulOp<U, V>::ResultType>
constexpr Vector<R, 3> operator*(const Quaternion<U>& q, const Vector<V, 3>& v)
{
    return v * (q.w * q.w - lm::length2(q.xyz)) + 2 * q.w * lm::cross(q.xyz, v)
        + 2 * q.xyz * lm::dot(q.xyz, v);
}

template<typename T>
constexpr double dot(const Quaternion<T>& a, const Quaternion<T>& b)
{
    return lm::dot(a.xyz, b.xyz) + a.w * b.w;
}

template<typename T>
Quaternion<T> normalize(Quaternion<T> a)
{
    T inv_norm = 1.0 / std::sqrt(lm::dot(a, a));
    a.xyz *= inv_norm;
    a.w *= inv_norm;
    return a;
}

template<typename T>
Quaternion<T> axis_angle(const Vector<T, 3>& axis, T angle)
{
    T c = std::cos(angle / 2);
    T s = std::sin(angle / 2);
    Vector<T, 3> a = normalize(axis) * s;
    return Quaternion<T>(a.x, a.y, a.z, c);
}

template<typename T>
Quaternion<T> rotation_between_vectors(Vector<T, 3> v0, Vector<T, 3> v1)
{
    v0 = lm::normalize(v0);
    v1 = lm::normalize(v1);

    if (v0 == -v1)
    {
        // Try to find an axis that is not colinear to our axes.
        Vector<T, 3> axis = lm::cross(v0, Vector<T, 3>(1, 0, 0));
        if (lm::length2(axis) == 0.0)
        {
            axis = lm::cross(v1, Vector<T, 3>(0, 1, 0));
        }
        return Quaternion<T>(lm::normalize(axis), (T)0);
    }
    else
    {
        Vector<T, 3> half = lm::normalize(v0 + v1);
        return Quaternion<T>(lm::cross(v0, half), lm::dot(v0, half));
    }
}

template<typename T>
constexpr Matrix<T, 4> rotation_normalized(const Quaternion<T>& q)
{
    return Matrix<T, 4>(
        lm::Vector<T, 4>(
            (T)1 - (T)2 * (q.y * q.y + q.z * q.z), (T)2 * (q.x * q.y + q.z * q.w),
            (T)2 * (q.x * q.z - q.y * q.w), 0),
        lm::Vector<T, 4>(
            (T)2 * (q.x * q.y - q.z * q.w), (T)1 - (T)2 * (q.x * q.x + q.z * q.z),
            (T)2 * (q.y * q.z + q.x * q.w), 0),
        lm::Vector<T, 4>(
            (T)2 * (q.x * q.z + q.y * q.w), (T)2 * (q.y * q.z - q.x * q.w),
            (T)1 - (T)2 * (q.x * q.x + q.y * q.y), 0),
        lm::Vector<T, 4>(0, 0, 0, 1));
}

template<typename T>
constexpr Matrix<T, 3> rotation3_normalized(const Quaternion<T>& q)
{
    return Matrix<T, 3>(
        lm::Vector<T, 3>(
            (T)1 - (T)2 * (q.y * q.y + q.z * q.z), (T)2 * (q.x * q.y + q.z * q.w),
            (T)2 * (q.x * q.z - q.y * q.w)),
        lm::Vector<T, 3>(
            (T)2 * (q.x * q.y - q.z * q.w), (T)1 - (T)2 * (q.x * q.x + q.z * q.z),
            (T)2 * (q.y * q.z + q.x * q.w)),
        lm::Vector<T, 3>(
            (T)2 * (q.x * q.z + q.y * q.w), (T)2 * (q.y * q.z - q.x * q.w),
            (T)1 - (T)2 * (q.x * q.x + q.y * q.y)));
}

template<typename T>
class PrecomputedSlerp
{
    Quaternion<T> _x, _z;
    T _angle{}, _inv_sin_angle{};
    bool _small_angle = true;

public:
    PrecomputedSlerp() = default;

    // Partially from https://www.geometrictools.com/GTE/Mathematics/Quaternion.h
    PrecomputedSlerp(const Quaternion<T>& x, const Quaternion<T>& z) : _x{x}, _z{z}
    {
        T cos_theta = dot(x, z);
        if (cos_theta < T{0})
        {
            _z.xyz = -z.xyz;
            _z.w = -z.w;
            cos_theta = -cos_theta;
        }

        _small_angle = cos_theta > T{1} - std::numeric_limits<T>::epsilon();
        if (!_small_angle)
        {
            _angle = (T)std::acos(cos_theta);
            _inv_sin_angle = T{1} / (T)std::sin(_angle);
        }
    }

    // Partially from https://www.geometrictools.com/GTE/Mathematics/Quaternion.h
    Quaternion<T> slerp(T t) const
    {
        Quaternion<T> res;

        T c1, c2;
        if (_small_angle)
        {
            c1 = T{1} - t;
            c2 = t;
        }
        else
        {
            c1 = (T)std::sin((T{1} - t) * _angle) * _inv_sin_angle;
            c2 = (T)std::sin(t * _angle) * _inv_sin_angle;
        }

        res.xyz = c1 * _x.xyz + c2 * _z.xyz;
        res.w = c1 * _x.w + c2 * _z.w;

        return normalize(res);
    }
};

template<typename T>
Quaternion<T> slerp(const Quaternion<T>& x, const Quaternion<T>& y, double a)
{
    PrecomputedSlerp<T> s(x, y);
    return s.slerp(a);
}

// From https://www.geometrictools.com/GTE/Mathematics/Rotation.h
template<typename T>
Quaternion<T> matrix_to_quaternion(const Matrix<T, 4>& matrix)
{
    Quaternion<T> q;
    const auto& r = matrix.m;

    T r22 = r[2][2];
    if (r22 <= (T)0) // x^2 + y^2 >= z^2 + w^2
    {
        T dif10 = r[1][1] - r[0][0];
        T omr22 = (T)1 - r22;

        if (dif10 <= (T)0) // x^2 >= y^2
        {
            T fourXSqr = omr22 - dif10;
            T inv4x = ((T)0.5) / std::sqrt(fourXSqr);
            q.m[0] = fourXSqr * inv4x;
            q.m[1] = (r[1][0] + r[0][1]) * inv4x;
            q.m[2] = (r[2][0] + r[0][2]) * inv4x;
            q.m[3] = (r[1][2] - r[2][1]) * inv4x;
        }
        else // y^2 >= x^2
        {
            T fourYSqr = omr22 + dif10;
            T inv4y = ((T)0.5) / std::sqrt(fourYSqr);
            q.m[0] = (r[1][0] + r[0][1]) * inv4y;
            q.m[1] = fourYSqr * inv4y;
            q.m[2] = (r[2][1] + r[1][2]) * inv4y;
            q.m[3] = (r[2][0] - r[0][2]) * inv4y;
        }
    }
    else // z^2 + w^2 >= x^2 + y^2
    {
        T sum10 = r[1][1] + r[0][0];
        T opr22 = (T)1 + r22;
        if (sum10 <= (T)0) // z^2 >= w^2
        {
            T fourZSqr = opr22 - sum10;
            T inv4z = ((T)0.5) / std::sqrt(fourZSqr);
            q.m[0] = (r[2][0] + r[0][2]) * inv4z;
            q.m[1] = (r[2][1] + r[1][2]) * inv4z;
            q.m[2] = fourZSqr * inv4z;
            q.m[3] = (r[0][1] - r[1][0]) * inv4z;
        }
        else // w^2 >= z^2
        {
            T fourWSqr = opr22 + sum10;
            T inv4w = ((T)0.5) / std::sqrt(fourWSqr);
            q.m[0] = (r[1][2] - r[2][1]) * inv4w;
            q.m[1] = (r[2][0] - r[0][2]) * inv4w;
            q.m[2] = (r[0][1] - r[1][0]) * inv4w;
            q.m[3] = fourWSqr * inv4w;
        }
    }

    return lm::normalize(q);
}

// @@DUAL_QUATERNIONS

template<typename T>
struct DualQuaternion
{
    Quaternion<T> r;
    Quaternion<T> d;

    constexpr DualQuaternion() : r{0, 0, 0, 1}, d{0, 0, 0, 0} {}

    constexpr explicit DualQuaternion(
        const Quaternion<T>& r_,
        const Quaternion<T>& d_ = Quaternion<T>{0, 0, 0, 0}) :
        r(r_), d(d_)
    {
    }

    constexpr explicit DualQuaternion(const Vector<T, 3>& v) : r{0, 0, 0, 1}, d{v.x, v.y, v.z, 0} {}
};

using dual_quat = DualQuaternion<float>;
using ddual_quat = DualQuaternion<double>;

template<typename T>
constexpr DualQuaternion<T> operator*(const DualQuaternion<T>& a, const DualQuaternion<T>& b)
{
    return DualQuaternion<T>(a.r * b.r, a.r * b.d + a.d * b.r);
}

template<typename T>
constexpr bool operator==(const DualQuaternion<T>& a, const DualQuaternion<T>& b)
{
    return a.r == b.r && a.d == b.d;
}

template<typename T>
constexpr DualQuaternion<T> translation_dquat(const Vector<T, 3>& p)
{
    return DualQuaternion<T>(
        Quaternion<T>(0, 0, 0, 1), Quaternion<T>(p.x / 2, p.y / 2, p.z / 2, 0.0));
}

template<typename T>
DualQuaternion<T> axis_angle_dquat(const Vector<T, 3>& p, float angle_rad)
{
    return DualQuaternion<T>(axis_angle<T>(p, angle_rad), Quaternion<T>(0, 0, 0, 0));
}

template<typename T>
DualQuaternion<T> rotation_position_dquat(const Quaternion<T>& r, const Vector<T, 3>& p)
{
    return DualQuaternion<T>(r, Quaternion<T>(p.x / 2, p.y / 2, p.z / 2, 0.0) * r);
}

// Inverses a dual quaternion assuming that the real part is a unit quaternion.
template<typename T>
DualQuaternion<T> inverse_assuming_unit(const DualQuaternion<T>& q)
{
    auto r_inv = lm::conjugate(q.r);
    auto d_inv = -(r_inv * q.d * r_inv);
    return DualQuaternion<T>(r_inv, d_inv);
}

template<typename T>
Vector<T, 3> extract_translation(const DualQuaternion<T>& q)
{
    return (q.d * conjugate(q.r)).xyz * 2;
}

template<typename T>
Matrix<T, 4> transform_matrix(const DualQuaternion<T>& q)
{
    Matrix<T, 3> r = rotation3_normalized(q.r);
    return Matrix<T, 4>(
        {r * lm::vec3(1, 0, 0), 0}, {r * lm::vec3(0, 1, 0), 0}, {r * lm::vec3(0, 0, 1), 0},
        {extract_translation(q), 1});
}

// @@BOUNDINGBOXES

template<typename T, int N>
struct Bbox
{
    Vector<T, N> min;
    Vector<T, N> max;

    constexpr Bbox() = default;

    constexpr explicit Bbox(const Vector<T, N>& min, const Vector<T, N>& max) : min{min}, max{max}
    {
    }

    template<std::convertible_to<T> U>
    constexpr explicit Bbox(const Vector<U, N>& min, const Vector<U, N>& max) : min{min}, max{max}
    {
    }

    static constexpr Bbox<T, N> invalid()
    {
        return Bbox<T, N>(
            Vector<T, N>(std::numeric_limits<T>::max()),
            Vector<T, N>(std::numeric_limits<T>::lowest()));
    }

    template<std::convertible_to<T> U>
    constexpr explicit(!ConvertibleToWithoutNarrowing<U, T>) Bbox(const Bbox<U, N>& other)
        requires(!std::same_as<T, U>)
        : min{other.min}, max{other.max}
    {
    }
};

using bbox2 = Bbox<float, 2>;
using bbox3 = Bbox<float, 3>;

using ibbox2 = Bbox<int32_t, 2>;
using ibbox3 = Bbox<int32_t, 3>;

using ubbox2 = Bbox<uint32_t, 2>;
using ubbox3 = Bbox<uint32_t, 3>;

using ilbbox2 = Bbox<int64_t, 2>;
using ilbbox3 = Bbox<int64_t, 3>;

using ulbbox2 = Bbox<uint64_t, 2>;
using ulbbox3 = Bbox<uint64_t, 3>;

using dbbox2 = Bbox<double, 2>;
using dbbox3 = Bbox<double, 3>;

template<typename U, typename V, int N>
constexpr bool operator==(const Bbox<U, N>& a, const Bbox<V, N>& b)
{
    return a.min == b.min && a.max == b.max;
}

template<typename T, int N>
constexpr Vector<T, N> size(const Bbox<T, N>& bb)
{
    return bb.max - bb.min;
}

template<typename T, int N>
constexpr T area(const Bbox<T, N>& bb)
{
    auto bb_size = size(bb);
    return bb_size.x * bb_size.y;
}

template<typename T, int N>
constexpr Vector<T, N> center(const Bbox<T, N>& bb)
{
    return (bb.min + bb.max) / (T)2;
}

template<typename T, int N>
inline T radius(const Bbox<T, N>& bb)
{
    return std::sqrt(radius2(bb));
}

template<typename T, int N>
inline T radius2(const Bbox<T, N>& bb)
{
    return 0.25 * (length2(abs(bb.max - bb.min)));
}

template<typename T, int N>
constexpr bool intersect(const Bbox<T, N>& left, const Bbox<T, N>& right)
{
    return !(
        left.max.x < right.min.x || left.min.x > right.max.x || left.max.y < right.min.y
        || left.min.y > right.max.y);
}

// Consider the intervals as open.
// @Todo Should the openness be part of the bbox definition?
template<typename T, int N>
constexpr bool intersect_open(const Bbox<T, N>& left, const Bbox<T, N>& right)
{
    return !(
        left.max.x <= right.min.x || left.min.x >= right.max.x || left.max.y <= right.min.y
        || left.min.y >= right.max.y);
}

template<typename T, int N>
constexpr Bbox<T, N> intersection(const Bbox<T, N>& left, const Bbox<T, N>& right)
{
    return Bbox<T, N>(max(left.min, right.min), min(left.max, right.max));
}

template<typename T, int N>
constexpr Bbox<T, N> expand(const Bbox<T, N>& bbox, const Vector<T, N>& v)
{
    return Bbox<T, N>(min(bbox.min, v), max(bbox.max, v));
}

template<typename T, int N>
constexpr Bbox<T, N> merge(const Bbox<T, N>& left, const Bbox<T, N>& right)
{
    Bbox<T, N> res{min(left.min, right.min), max(left.max, right.max)};
    return res;
}

template<typename T, int N>
constexpr Bbox<T, N> merge(const Vector<T, N>& a, const Vector<T, N>& b)
{
    Bbox<T, N> res{min(a, b), max(a, b)};
    return res;
}

template<typename T, typename U, int N>
constexpr bool contains(const Bbox<T, N>& bbox, const Vector<U, N>& v)
{
    bool is_inside = true;
    for (unsigned int i = 0; i < N; ++i)
    {
        is_inside &= v.m[i] >= bbox.min.m[i] && v.m[i] <= bbox.max.m[i];
    }
    return is_inside;
}

template<typename T, int N>
constexpr bool is_valid(const Bbox<T, N>& bbox)
{
    return all(bbox.min <= bbox.max);
}

// This computes all 2^N corners of a bounding box. Each bit in the mask
// corresponds to a dimension, starting with the lowest bit. Any bit higher than
// the bounding box dimension is ignored. When the bit is 0 the min value is
// used, when the bit is 1 the max value is used. For instance for a 2
// dimensional bbox, 0 is lower-left, 1 is lower-right, 2 is upper-left and 3 is
// upper-right. If you cross your fingers hard enough, your compiler may
// optimize this away. Clang accepts constexpr on this function but not all
// compilers do for now.
template<typename T, int N>
constexpr Vector<T, N> corner(const Bbox<T, N>& bbox, uint32_t mask)
{
    Vector<T, N> c;
    for (int i = 0; i < N; ++i)
    {
        c.m[i] = (mask & (1U << i)) ? bbox.max.m[i] : bbox.min.m[i];
    }
    return c;
}

} // namespace lm
