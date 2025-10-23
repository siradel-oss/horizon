import argparse
import sys

# Generates a prefix tree encoding all HTML colors.
# Then encode it as a series of C++ functions that lookup this tree.

def generate_prefix_tree(node, word, value):
    if len(word) == 0:
        node[""] = value
        return
    if word[0] not in node:
        node[word[0]] = {}
    generate_prefix_tree(node[word[0]], word[1:], value)

def compress_prefix_tree(node):
    for child in node.values():
        if isinstance(child, dict):
            compress_prefix_tree(child)

    to_merge = [word for word in node if len(node[word]) == 1 and isinstance(node[word], dict)]

    for word in to_merge:
        child = list(node[word].items())[0]
        del node[word]
        node[word + child[0]] = child[1]

def generate_lookup_tree(colors):
    tree = {}
    for c in colors:
        generate_prefix_tree(tree, c[0], c[1])
    compress_prefix_tree(tree)
    return tree

def generate_lookup_function(node, parent_prefix = ""):
    code_before = ""
    func_suffix = "_" + parent_prefix if len(parent_prefix) > 0 else ""
    code = f"std::optional<uint32_t> html_color_lookup{func_suffix}(std::string_view str)\n"
    code += "{\n"
    if "" in node:
        code += "    if (str.empty()) { return " + str(node[""]) + "; }\n"
    else:
        code += "    if (str.empty()) return std::nullopt;\n"
    code += "    switch (str[0])\n"
    code += "    {\n"
    for prefix, child in node.items():
        if prefix == "": continue
        code += f"        case '{prefix[0]}':\n"
        if isinstance(child, dict):
            full_prefix = parent_prefix + prefix
            code += f"            if (str.starts_with(\"{prefix}\")) {{ return html_color_lookup_{full_prefix}(str.substr({len(prefix)})); }}\n"
            code_before += generate_lookup_function(child, full_prefix)
        else:
            code += f"            if (str == \"{prefix}\") {{ return {child}; }}\n"
        code += "            break;\n"
    code += "    }\n"
    code += "    return std::nullopt;\n"
    code += "}\n\n"
    return code_before + code

def generate_lookup_code(tree):
    max_length = max(len(c[0]) for c in COLORS)
    code = f"static constexpr size_t kMaxHtmlColorLength = {max_length};\n\n"
    code += generate_lookup_function(tree)
    return code

# Colours from https://www.w3.org/TR/css-color-3/#svg-color
COLORS = [["aliceblue", "#f0f8ff"],
    ["antiquewhite", "#faebd7"],
    ["aqua", "#00ffff"],
    ["aquamarine", "#7fffd4"],
    ["azure", "#f0ffff"],
    ["beige", "#f5f5dc"],
    ["bisque", "#ffe4c4"],
    ["black", "#000000"],
    ["blanchedalmond", "#ffebcd"],
    ["blue", "#0000ff"],
    ["blueviolet", "#8a2be2"],
    ["brown", "#a52a2a"],
    ["burlywood", "#deb887"],
    ["cadetblue", "#5f9ea0"],
    ["chartreuse", "#7fff00"],
    ["chocolate", "#d2691e"],
    ["coral", "#ff7f50"],
    ["cornflowerblue", "#6495ed"],
    ["cornsilk", "#fff8dc"],
    ["crimson", "#dc143c"],
    ["cyan", "#00ffff"],
    ["darkblue", "#00008b"],
    ["darkcyan", "#008b8b"],
    ["darkgoldenrod", "#b8860b"],
    ["darkgray", "#a9a9a9"],
    ["darkgreen", "#006400"],
    ["darkgrey", "#a9a9a9"],
    ["darkkhaki", "#bdb76b"],
    ["darkmagenta", "#8b008b"],
    ["darkolivegreen", "#556b2f"],
    ["darkorange", "#ff8c00"],
    ["darkorchid", "#9932cc"],
    ["darkred", "#8b0000"],
    ["darksalmon", "#e9967a"],
    ["darkseagreen", "#8fbc8f"],
    ["darkslateblue", "#483d8b"],
    ["darkslategray", "#2f4f4f"],
    ["darkslategrey", "#2f4f4f"],
    ["darkturquoise", "#00ced1"],
    ["darkviolet", "#9400d3"],
    ["deeppink", "#ff1493"],
    ["deepskyblue", "#00bfff"],
    ["dimgray", "#696969"],
    ["dimgrey", "#696969"],
    ["dodgerblue", "#1e90ff"],
    ["firebrick", "#b22222"],
    ["floralwhite", "#fffaf0"],
    ["forestgreen", "#228b22"],
    ["fuchsia", "#ff00ff"],
    ["gainsboro", "#dcdcdc"],
    ["ghostwhite", "#f8f8ff"],
    ["gold", "#ffd700"],
    ["goldenrod", "#daa520"],
    ["gray", "#808080"],
    ["green", "#008000"],
    ["greenyellow", "#adff2f"],
    ["grey", "#808080"],
    ["honeydew", "#f0fff0"],
    ["hotpink", "#ff69b4"],
    ["indianred", "#cd5c5c"],
    ["indigo", "#4b0082"],
    ["ivory", "#fffff0"],
    ["khaki", "#f0e68c"],
    ["lavender", "#e6e6fa"],
    ["lavenderblush", "#fff0f5"],
    ["lawngreen", "#7cfc00"],
    ["lemonchiffon", "#fffacd"],
    ["lightblue", "#add8e6"],
    ["lightcoral", "#f08080"],
    ["lightcyan", "#e0ffff"],
    ["lightgoldenrodyellow", "#fafad2"],
    ["lightgray", "#d3d3d3"],
    ["lightgreen", "#90ee90"],
    ["lightgrey", "#d3d3d3"],
    ["lightpink", "#ffb6c1"],
    ["lightsalmon", "#ffa07a"],
    ["lightseagreen", "#20b2aa"],
    ["lightskyblue", "#87cefa"],
    ["lightslategray", "#778899"],
    ["lightslategrey", "#778899"],
    ["lightsteelblue", "#b0c4de"],
    ["lightyellow", "#ffffe0"],
    ["lime", "#00ff00"],
    ["limegreen", "#32cd32"],
    ["linen", "#faf0e6"],
    ["magenta", "#ff00ff"],
    ["maroon", "#800000"],
    ["mediumaquamarine", "#66cdaa"],
    ["mediumblue", "#0000cd"],
    ["mediumorchid", "#ba55d3"],
    ["mediumpurple", "#9370db"],
    ["mediumseagreen", "#3cb371"],
    ["mediumslateblue", "#7b68ee"],
    ["mediumspringgreen", "#00fa9a"],
    ["mediumturquoise", "#48d1cc"],
    ["mediumvioletred", "#c71585"],
    ["midnightblue", "#191970"],
    ["mintcream", "#f5fffa"],
    ["mistyrose", "#ffe4e1"],
    ["moccasin", "#ffe4b5"],
    ["navajowhite", "#ffdead"],
    ["navy", "#000080"],
    ["oldlace", "#fdf5e6"],
    ["olive", "#808000"],
    ["olivedrab", "#6b8e23"],
    ["orange", "#ffa500"],
    ["orangered", "#ff4500"],
    ["orchid", "#da70d6"],
    ["palegoldenrod", "#eee8aa"],
    ["palegreen", "#98fb98"],
    ["paleturquoise", "#afeeee"],
    ["palevioletred", "#db7093"],
    ["papayawhip", "#ffefd5"],
    ["peachpuff", "#ffdab9"],
    ["peru", "#cd853f"],
    ["pink", "#ffc0cb"],
    ["plum", "#dda0dd"],
    ["powderblue", "#b0e0e6"],
    ["purple", "#800080"],
    ["red", "#ff0000"],
    ["rosybrown", "#bc8f8f"],
    ["royalblue", "#4169e1"],
    ["saddlebrown", "#8b4513"],
    ["salmon", "#fa8072"],
    ["sandybrown", "#f4a460"],
    ["seagreen", "#2e8b57"],
    ["seashell", "#fff5ee"],
    ["sienna", "#a0522d"],
    ["silver", "#c0c0c0"],
    ["skyblue", "#87ceeb"],
    ["slateblue", "#6a5acd"],
    ["slategray", "#708090"],
    ["slategrey", "#708090"],
    ["snow", "#fffafa"],
    ["springgreen", "#00ff7f"],
    ["steelblue", "#4682b4"],
    ["tan", "#d2b48c"],
    ["teal", "#008080"],
    ["thistle", "#d8bfd8"],
    ["tomato", "#ff6347"],
    ["turquoise", "#40e0d0"],
    ["violet", "#ee82ee"],
    ["wheat", "#f5deb3"],
    ["white", "#ffffff"],
    ["whitesmoke", "#f5f5f5"],
    ["yellow", "#ffff00"],
    ["yellowgreen", "#9acd32"]]

for c in COLORS:
    c[1] = "0xff{}{}{}U".format(c[1][5:], c[1][3:5], c[1][1:3])

def lookup_cmd():
    tree = generate_lookup_tree(COLORS)
    print(generate_lookup_code(tree))

def randomize_case(s):
    import random
    return "".join(c.upper() if random.random() > 0.5 else c.lower() for c in s)

def tests_cmd():
    color_names = ", ".join([f'"{randomize_case(c[0])}"' for c in COLORS])
    expected_values = ", ".join([c[1] for c in COLORS])
    print(f"{{")
    print(f"    // Generated by generate_html_colors_lookup.py")
    print(f"    static const char* color_names[] = {{{color_names}}};")
    print(f"")
    print(f"    static const uint32_t expected_values[] = {{{expected_values}}};")
    print(f"")
    print(f"    size_t names_size = sizeof(color_names) / sizeof(color_names[0]);")
    print(f"    size_t values_size = sizeof(expected_values) / sizeof(expected_values[0]);")
    print(f"")
    print(f"    ASSERT_EQ(names_size, values_size);")
    print(f"")
    print(f"    for (size_t i = 0; i < names_size; ++i)")
    print(f"    {{")
    print(f"        ASSERT_EQ(hrz_mapbox::get_html_color_value(color_names[i], strlen(color_names[i])), expected_values[i]);")
    print(f"    }}")
    print(f"}}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser_lookup = subparsers.add_parser("lookup")
    parser_lookup.set_defaults(func = lookup_cmd)

    parser_lookup = subparsers.add_parser("tests")
    parser_lookup.set_defaults(func = tests_cmd)

    parser.parse_args(sys.argv[1:]).func()

