#include "cli.hpp"

#include <charconv>
#include <cstdlib>

namespace ml {

std::string usage() {
    return R"(memlayout - draw the real memory layout of C++ types

USAGE
  memlayout [options] <source.cpp | -> [Type ...]

  Compiles <source.cpp> with clang, reads the layout the compiler actually chose
  (sizes, alignment, padding, vptrs, base classes, bitfields) and draws it.
  With no Type given, every struct/class/union defined in the file is drawn.
  Use '-' to read the source from stdin. Template instances can be named
  explicitly, e.g.  memlayout file.cpp 'Box<int, 3>'.

OUTPUT
  -f, --format <fmt>     term (default) | svg | table | json
  -o, --output <path>    file for one type, or a directory for several
                         (svg defaults to stdout for one type, ./<Type>.svg for several)
      --theme <t>        dark (default) | light              (svg, term)
      --color <mode>     auto (default) | always | never     (term)
  -w, --bytes-per-row N  wrap rows after N bytes (default 16)
  -d, --depth N          expand nested struct members N levels deep (default 1)
      --no-title         omit the heading line in SVG output

COMPILER
      --cxx <path>       compiler to use (default: $CXX, else clang++; must be clang)
      --std <std>        language standard (default c++23)
  -I<dir> -D<macro> -include <file> -m32 --target=<t> ...
                         anything after `--` is passed to the compiler verbatim;
                         -I/-D are accepted directly as well
      --no-probe         do not build/run a probe program; infer sizes from offsets
                         (needed when cross-compiling to a target you cannot run)

MISC
      --list             list records that can be drawn and exit
  -h, --help | -V, --version

EXAMPLES
  memlayout examples/basic.cpp Sample
  memlayout examples/basic.cpp Sample -f svg -o sample.svg
  memlayout shapes.cpp --depth 2 --theme light -f svg -o out/
)";
}

namespace {
struct ArgError { std::string msg; };

Options parse_args_impl(std::span<const char* const> args) {
    Options o;
    std::vector<std::string> positional;
    auto fail = [](std::string m) -> Options { throw ArgError{std::move(m)}; };

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];
        std::optional<std::string> inline_val;
        if (a.starts_with("--")) {
            if (const auto eq = a.find('='); eq != std::string::npos) {
                inline_val = a.substr(eq + 1);
                a.resize(eq);
            }
        }
        auto value = [&]() -> std::string {
            if (inline_val) return *inline_val;
            if (i + 1 >= args.size()) throw ArgError{"option " + a + " needs a value"};
            return std::string(args[++i]);
        };
        auto number = [&](int& dst, int min) -> std::optional<std::string> {
            const std::string v = value();
            int n = 0;
            const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
            if (ec != std::errc{} || p != v.data() + v.size() || n < min)
                return "invalid number for " + a + ": '" + v + "'";
            dst = n;
            return std::nullopt;
        };

        if (a == "--") {
            for (++i; i < args.size(); ++i) o.compiler_args.emplace_back(args[i]);
        } else if (a == "-h" || a == "--help") {
            o.help = true;
        } else if (a == "-V" || a == "--version") {
            o.version = true;
        } else if (a == "--list") {
            o.list = true;
        } else if (a == "--no-probe") {
            o.probe = false;
        } else if (a == "--no-title") {
            o.title = false;
        } else if (a == "-f" || a == "--format") {
            const std::string v = value();
            if (v == "term") o.format = Format::Term;
            else if (v == "svg") o.format = Format::Svg;
            else if (v == "table") o.format = Format::Table;
            else if (v == "json") o.format = Format::Json;
            else fail("unknown format '" + v + "' (term, svg, table, json)");
        } else if (a == "-o" || a == "--output") {
            const std::string v = value();
            o.output = v;
        } else if (a == "--theme") {
            const std::string v = value();
            if (v == "dark") o.theme = Theme::Dark;
            else if (v == "light") o.theme = Theme::Light;
            else fail("unknown theme '" + v + "' (dark, light)");
        } else if (a == "--color") {
            const std::string v = value();
            if (v == "auto") o.color = ColorMode::Auto;
            else if (v == "always") o.color = ColorMode::Always;
            else if (v == "never") o.color = ColorMode::Never;
            else fail("unknown color mode '" + v + "' (auto, always, never)");
        } else if (a == "-w" || a == "--bytes-per-row") {
            if (auto e = number(o.bytes_per_row, 1)) fail(*e);
        } else if (a == "-d" || a == "--depth") {
            if (auto e = number(o.depth, 1)) fail(*e);
        } else if (a == "--cxx") {
            const std::string v = value();
            o.cxx = v;
        } else if (a == "--std") {
            const std::string v = value();
            o.std_flag = v;
        } else if (a == "-I" || a == "-D" || a == "-include") {
            const std::string v = value();
            o.compiler_args.push_back(a);
            o.compiler_args.push_back(v);
        } else if (a.starts_with("-I") || a.starts_with("-D") || a == "-m32" || a == "-m64" ||
                   a.starts_with("--target=") || a.starts_with("-isystem") || a.starts_with("-stdlib=")) {
            o.compiler_args.push_back(a);
        } else if (a.size() > 1 && a.starts_with('-')) {
            fail("unknown option '" + a + "' (see --help)");
        } else {
            positional.push_back(std::move(a));
        }
    }
    if (o.help || o.version) return o;
    if (positional.empty()) fail("missing input file (see --help)");
    o.input = positional.front();
    o.types.assign(positional.begin() + 1, positional.end());
    if (o.cxx.empty()) {
        const char* env = std::getenv("CXX");
        o.cxx = env && *env ? env : "clang++";
    }
    return o;
}

}  // namespace

ParseResult parse_args(std::span<const char* const> args) {
    try {
        return {parse_args_impl(args), {}};
    } catch (const ArgError& e) {
        return {{}, e.msg};
    }
}

}  // namespace ml
