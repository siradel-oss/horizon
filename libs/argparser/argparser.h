#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace argparser
{
struct ArgParser;

enum class ArgType
{
    Bool,
    Float,
    Int,
    Uint,
    Double,
    String,
};

struct ArgDef
{
    std::string name;
    ArgType type;
    bool required;
    bool has_default;

    union
    {
        const char* default_string;
        uint64_t default_uint;
        int64_t default_int;
        bool default_bool;
        double default_double;
    };
};

ArgParser* create();

void destroy(ArgParser*);

void add_argument(ArgParser*, const ArgDef& arg);

bool parse(ArgParser*, int argc, char* argv[]);
void show_help(const ArgParser*);

std::optional<bool> get_value_bool(ArgParser*, const char* arg_name);
std::optional<int64_t> get_value_int(ArgParser*, const char* arg_name);
std::optional<uint64_t> get_value_uint(ArgParser*, const char* arg_name);
std::optional<double> get_value_double(ArgParser*, const char* arg_name);
std::optional<const char*> get_value_string(ArgParser*, const char* arg_name);

} // namespace argparser
