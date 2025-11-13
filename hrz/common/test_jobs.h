#pragma once

#include <cstdint>
#include <string>

namespace hrz::test
{
struct TestJob1Params
{
    int32_t a;
    int32_t b;
};

struct TestJob2Params
{
    std::string s;
    int32_t count;
};

struct TestJob1Response
{
    int32_t v;
};

struct TestJob2Response
{
    std::string res;
};
} // namespace hrz::test
