#include "argparser.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include <functional>
#include <stdio.h>
#include <unordered_map>
#include <vector>

namespace argparser
{
struct ParsedValue
{
    bool is_set = false;

    union
    {
        int64_t value_int;
        uint64_t value_uint;
        bool value_bool;
        double value_double;
    };

    std::string value_string;
};

struct Arg
{
    const ArgDef def;
    ParsedValue value;

    explicit Arg(const ArgDef& def_) : def{def_}, value{} {}
};

struct ArgParser;
using ActionFn = std::function<bool(ArgParser*, const char* next_token)>;

struct ArgParser
{
    std::unordered_map<std::string, ActionFn> actions;
    std::unordered_map<std::string, int> name_to_arg_index;
    std::vector<Arg> args;
};

ArgParser* create()
{
    return new ArgParser();
}

void destroy(ArgParser* ap)
{
    delete ap;
}

void add_argument(ArgParser* ap, const ArgDef& arg)
{
    assert(ap);

    if (arg.required && arg.has_default)
    {
        printf("Required argument '%s' cannot have a default value\n", arg.name.c_str());
        return;
    }

    auto it = ap->name_to_arg_index.find(arg.name);
    if (it != std::end(ap->name_to_arg_index))
    {
        printf("Trying to add duplicate argument '%s'\n", arg.name.c_str());
        return;
    }

    ap->args.push_back(Arg(arg));
    int index = (int)(ap->args.size() - 1);

    ap->name_to_arg_index[arg.name] = index;

    switch (arg.type)
    {
        case ArgType::Switch:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                ap->args[index].value.is_set = true;
                return true;
            };
            break;
        }
        case ArgType::Bool:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_bool = true;
                return true;
            };
            ap->actions[std::string("no-") + arg.name] =
                [index](ArgParser* ap, const char* next_token) -> bool
            {
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_bool = false;
                return true;
            };
            break;
        }
        case ArgType::Float:
        case ArgType::Double:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                if (!next_token) return false;
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_double = strtod(next_token, nullptr);
                return true;
            };
            break;
        }
        case ArgType::Int:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                if (!next_token) return false;
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_int = strtoll(next_token, nullptr, 10);
                return true;
            };
            break;
        }
        case ArgType::Uint:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                if (!next_token) return false;
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_uint = strtoull(next_token, nullptr, 10);
                return true;
            };
            break;
        }
        case ArgType::String:
        {
            ap->actions[arg.name] = [index](ArgParser* ap, const char* next_token) -> bool
            {
                if (!next_token) return false;
                ap->args[index].value.is_set = true;
                ap->args[index].value.value_string = next_token;
                return true;
            };
            break;
        }
    }
}

bool parse(ArgParser* ap, int argc, char* argv[], bool check_required_args)
{
    assert(ap);

    for (int i = 0; i < argc; ++i)
    {
        size_t arg_length = strlen(argv[i]);
        if (arg_length <= 2 || (argv[i][0] != '-' && argv[i][1] != '-')) continue;

        auto it = ap->actions.find(argv[i] + 2);
        if (it == std::end(ap->actions))
        {
            printf("Skipping unknown argument '%s'\n", argv[i]);
            continue;
        }

        const char* next_token = (i < argc - 1) ? argv[i + 1] : nullptr;
        if (!it->second(ap, next_token))
        {
            printf("Invalid argument for '%s'\n", argv[i]);
            return false;
        }
    }

    if (check_required_args && !check_required(ap))
    {
        return false;
    }

    return true;
}

bool check_required(ArgParser* ap)
{
    assert(ap);

    for (const Arg& arg : ap->args)
    {
        if (arg.def.required && !arg.value.is_set)
        {
            printf("Missing required argument '%s'\n", arg.def.name.c_str());
            return false;
        }
    }

    return true;
}

template<typename T>
T get_value_raw(ParsedValue& value, ArgType type)
{
    switch (type)
    {
        case ArgType::Bool: return value.value_bool;
        case ArgType::Int: return value.value_int;
        case ArgType::Uint: return value.value_uint;
        case ArgType::Double: return value.value_double;
        default: assert(false && "Unknown type");
    }
    return (T)0;
}

template<typename T>
T get_default_value(const ArgDef& arg)
{
    switch (arg.type)
    {
        case ArgType::Bool: return arg.default_bool;
        case ArgType::Int: return arg.default_int;
        case ArgType::Uint: return arg.default_uint;
        case ArgType::Double: return arg.default_double;
        default: assert(false && "Unknown type");
    }
    return (T)0;
}

template<typename T>
std::optional<T> get_value(ArgParser* ap, const char* arg_name, ArgType expected_type)
{
    assert(ap);

    auto it = ap->name_to_arg_index.find(arg_name);
    if (it == std::end(ap->name_to_arg_index))
    {
        return std::nullopt;
    }

    Arg& arg = ap->args[it->second];
    if (arg.def.type != expected_type)
    {
        assert(false && "Requested type and argument type don't match.\n");
        return std::nullopt;
    }

    if (arg.value.is_set)
    {
        return get_value_raw<T>(arg.value, arg.def.type);
    }

    if (arg.def.has_default)
    {
        return get_default_value<T>(arg.def);
    }

    return std::nullopt;
}

bool get_switch(ArgParser* ap, const char* arg_name)
{
    assert(ap);

    auto it = ap->name_to_arg_index.find(arg_name);
    if (it == std::end(ap->name_to_arg_index))
    {
        return false;
    }

    Arg& arg = ap->args[it->second];
    if (arg.def.type != ArgType::Switch)
    {
        assert(false && "Requested type and argument type don't match.\n");
        return false;
    }

    return arg.value.is_set;
}

std::optional<bool> get_value_bool(ArgParser* ap, const char* arg_name)
{
    return get_value<bool>(ap, arg_name, ArgType::Bool);
}

std::optional<int64_t> get_value_int(ArgParser* ap, const char* arg_name)
{
    return get_value<int64_t>(ap, arg_name, ArgType::Int);
}

std::optional<uint64_t> get_value_uint(ArgParser* ap, const char* arg_name)
{
    return get_value<uint64_t>(ap, arg_name, ArgType::Uint);
}

std::optional<double> get_value_double(ArgParser* ap, const char* arg_name)
{
    return get_value<double>(ap, arg_name, ArgType::Double);
}

std::optional<const char*> get_value_string(ArgParser* ap, const char* arg_name)
{
    assert(ap);

    auto it = ap->name_to_arg_index.find(arg_name);
    if (it == std::end(ap->name_to_arg_index))
    {
        return std::nullopt;
    }

    Arg& arg = ap->args[it->second];
    if (arg.def.type != ArgType::String)
    {
        assert(false && "Requested type and argument type don't match.\n");
        return std::nullopt;
    }

    if (arg.value.is_set)
    {
        return arg.value.value_string.c_str();
    }

    if (!arg.def.required)
    {
        return arg.def.default_string;
    }

    return std::nullopt;
}

void show_help(const ArgParser* ap)
{
    printf("Available arguments:\n");
    for (const auto& arg : ap->args)
    {
        auto print_required = [&]()
        {
            if (arg.def.required)
            {
                printf("        required\n");
            }
        };

        switch (arg.def.type)
        {
            case ArgType::Switch: printf("--%s\n", arg.def.name.c_str()); break;
            case ArgType::Bool:
                printf("--[no-]%s\n", arg.def.name.c_str());
                print_required();
                if (arg.def.has_default)
                {
                    printf("        default: %s\n", arg.def.default_bool ? "enabled" : "disabled");
                }
                break;
            case ArgType::Int:
                printf("--%s <integer>\n", arg.def.name.c_str());
                print_required();
                if (arg.def.has_default)
                {
                    printf("        default: %" PRIi64 "\n", arg.def.default_int);
                }
                break;
            case ArgType::Uint:
                printf("--%s <unsigned integer>\n", arg.def.name.c_str());
                print_required();
                if (arg.def.has_default)
                {
                    printf("        default: %" PRIu64 "\n", arg.def.default_uint);
                }
                break;
            case ArgType::Float:
            case ArgType::Double:
                printf("--%s <real number>\n", arg.def.name.c_str());
                print_required();
                if (arg.def.has_default)
                {
                    printf("        default: %lf\n", arg.def.default_double);
                }
                break;
            case ArgType::String:
                printf("--%s <string>\n", arg.def.name.c_str());
                print_required();
                if (arg.def.has_default)
                {
                    printf("        default: \"%s\"\n", arg.def.default_string);
                }
                break;
        }

        printf("\n");
    }
}

} // namespace argparser
