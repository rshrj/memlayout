#pragma once
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "render.hpp"

namespace ml {

enum class Format { Term, Svg, Json, Table };
enum class ColorMode { Auto, Always, Never };

struct Options {
    std::string input;                 // path, or "-" for stdin
    std::vector<std::string> types;    // empty: every record defined in the input
    Format format = Format::Term;
    Theme theme = Theme::Dark;
    ColorMode color = ColorMode::Auto;
    std::optional<std::string> output; // file (one type) or directory
    int bytes_per_row = 16;
    int depth = 1;
    std::string cxx;                   // compiler (default: $CXX or clang++)
    std::string std_flag = "c++23";
    std::vector<std::string> compiler_args;
    bool probe = true;
    bool list = false;
    bool title = true;
    bool help = false;
    bool version = false;
};

// Result of argument parsing: `error` is empty on success.
struct ParseResult {
    Options options;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

ParseResult parse_args(std::span<const char* const> args);
std::string usage();
inline constexpr const char* kVersion = "0.1.0";

}  // namespace ml
