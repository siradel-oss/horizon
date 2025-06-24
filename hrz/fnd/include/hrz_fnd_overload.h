#pragma once

namespace hrz
{

template<typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};

template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

} // namespace hrz
