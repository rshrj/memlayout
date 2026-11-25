// memlayout: draw the real memory layout of C++ types, as chosen by the compiler.
//
// Pipeline:  source --clang dump--> records --flatten--> items --probe--> sizes --> figure --> render
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <regex>
#include <set>

#include "cli.hpp"
#include "clang_dump.hpp"
#include "figure.hpp"
#include "layout.hpp"
#include "process.hpp"
#include "render.hpp"

namespace fs = std::filesystem;
using namespace ml;

namespace {

[[noreturn]] void die(const std::string& msg) {
    std::cerr << "memlayout: " << msg << '\n';
    std::exit(1);
}

std::string read_stdin() {
    std::string s;
    char buf[4096];
    while (std::cin.read(buf, sizeof buf) || std::cin.gcount() > 0) s.append(buf, static_cast<std::size_t>(std::cin.gcount()));
    return s;
}

// Names of struct/class/union *definitions* found textually in the source.
std::vector<std::string> declared_records(const std::string& src) {
    static const std::regex re(R"(\b(?:struct|class|union)\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{]*)?\{)");
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it)
        if (seen.insert((*it)[1]).second) names.push_back((*it)[1]);
    return names;
}

std::string safe_filename(std::string n) {
    for (char& c : n)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
    return n;
}

const char* ext(Format f) {
    switch (f) {
    case Format::Svg: return "svg";
    case Format::Json: return "json";
    default: return "txt";
    }
}

}  // namespace

int main(int argc, char** argv) {
    auto parsed = parse_args(std::span<const char* const>(argv + 1, static_cast<std::size_t>(argc - 1)));
    if (!parsed) die(parsed.error());
    Options opt = std::move(*parsed);
    if (opt.help) { std::cout << usage(); return 0; }
    if (opt.version) { std::cout << "memlayout " << kVersion << '\n'; return 0; }

    TempDir tmp;

    // ---- 1. locate the source --------------------------------------------------
    fs::path source;
    std::string source_text;
    if (opt.input == "-") {
        source_text = read_stdin();
        source = tmp.path() / "stdin.cpp";
        write_file(source, source_text);
    } else {
        source = fs::absolute(opt.input);
        if (!fs::exists(source)) die("no such file: " + opt.input);
        source_text = read_file(source);
    }

    // ---- 2. decide which types to draw ----------------------------------------
    const bool explicit_types = !opt.types.empty();
    std::vector<std::string> wanted = explicit_types ? opt.types : declared_records(source_text);
    if (wanted.empty() && !opt.list) die("no struct/class/union definitions found; name a type explicitly");

    // The user's `main` is renamed so we can add our own; explicitly requested types
    // are instantiated (templates!) by taking their sizeof at namespace scope.
    std::string wrapper = "#define main __ml_user_main\n#include \"" + source.string() + "\"\n#undef main\n";
    if (explicit_types)
        for (std::size_t i = 0; i < wanted.size(); ++i)
            wrapper += std::format("[[maybe_unused]] inline constexpr unsigned long __ml_force_{} = sizeof({});\n", i, wanted[i]);

    std::vector<std::string> base_args{opt.cxx, "-std=" + opt.std_flag, "-w", "-Xclang", "-fno-access-control",
                                       "-I", source.parent_path().string()};
    base_args.insert(base_args.end(), opt.compiler_args.begin(), opt.compiler_args.end());

    // ---- 3. ask the compiler for the layouts ----------------------------------
    write_file(tmp.path() / "dump.cpp", wrapper);
    auto dump_cmd = base_args;
    dump_cmd.insert(dump_cmd.end(), {"-fsyntax-only", "-Xclang", "-fdump-record-layouts-complete", (tmp.path() / "dump.cpp").string()});
    const RunResult dump = run(dump_cmd, tmp.path());
    if (dump.exit_code != 0) {
        std::cerr << dump.err;
        die(dump.err.find("unknown argument") != std::string::npos || dump.err.find("unsupported option") != std::string::npos
                ? "the compiler rejected a clang-specific flag; use --cxx to point at clang++"
                : "compilation failed (see errors above)");
    }
    const std::vector<Record> records = parse_record_layouts(dump.out);

    // ---- 4. resolve names to records ------------------------------------------
    std::vector<const Record*> chosen;
    for (const auto& name : wanted) {
        const FindResult r = find_record(records, name);
        if (r.status == FindStatus::Found) {
            if (std::ranges::find(chosen, r.record) == chosen.end()) chosen.push_back(r.record);
        } else if (r.status == FindStatus::Ambiguous) {
            std::string c;
            for (const auto& s : r.candidates) c += "\n  " + s;
            die("'" + name + "' is ambiguous; qualify it. Candidates:" + c);
        } else if (explicit_types) {
            die("no layout for '" + name + "' (is it complete and defined in the file?)");
        } else {
            std::cerr << "memlayout: skipping '" << name << "' (no layout: template or incomplete type)\n";
        }
    }
    if (opt.list) {
        for (const Record* r : chosen) std::cout << std::format("{} {}  (sizeof {}, alignof {})\n", r->keyword, r->name, r->size, r->align);
        return 0;
    }
    if (chosen.empty()) die("nothing to draw");

    // ---- 5. measure exact member sizes (compile + run a probe) ----------------
    FlattenOptions fo{.depth = opt.depth, .pointer_size = 8};
    std::vector<std::vector<ProbeResult>> results(chosen.size());
    bool probed = false;
    if (opt.probe) {
        std::vector<ProbeRequest> all;
        std::vector<std::size_t> counts;
        for (const Record* r : chosen) {
            auto reqs = collect_probes(*r, fo);
            counts.push_back(reqs.size());
            all.insert(all.end(), std::make_move_iterator(reqs.begin()), std::make_move_iterator(reqs.end()));
        }
        write_file(tmp.path() / "probe.cpp", wrapper + "#include <cstddef>\n" + make_probe_main(all));
        auto cmd = base_args;
        cmd.insert(cmd.end(), {"-O0", (tmp.path() / "probe.cpp").string(), "-o", (tmp.path() / "probe").string()});
        const RunResult build = run(cmd, tmp.path());
        RunResult exec;
        if (build.exit_code == 0) exec = run({(tmp.path() / "probe").string()}, tmp.path());
        if (build.exit_code == 0 && exec.exit_code == 0) {
            std::vector<ProbeResult> flat;
            std::size_t a = 0, b = 0;
            std::string_view text = exec.out;
            while (!text.empty()) {
                const auto nl = text.find('\n');
                const std::string line(text.substr(0, nl));
                if (std::sscanf(line.c_str(), "%zu %zu", &a, &b) == 2) flat.push_back({a, b});
                text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
            }
            if (flat.size() == all.size() + 1) {
                fo.pointer_size = flat[0].size;
                std::size_t pos = 1;
                for (std::size_t i = 0; i < chosen.size(); ++i) {
                    results[i].assign(flat.begin() + static_cast<long>(pos), flat.begin() + static_cast<long>(pos + counts[i]));
                    pos += counts[i];
                }
                probed = true;
            }
        }
        if (!probed) {
            std::cerr << "memlayout: warning: could not build/run the size probe; inferring sizes from offsets\n"
                      << "           (use --no-probe to silence; see compiler output below)\n"
                      << build.err.substr(0, 1500);
        }
    }

    // ---- 6. build figures and render ------------------------------------------
    RenderOptions ro{.bytes_per_row = opt.bytes_per_row, .theme = opt.theme, .color = false, .title = opt.title};
    ro.color = opt.color == ColorMode::Always || (opt.color == ColorMode::Auto && isatty(STDOUT_FILENO) && !opt.output);

    std::vector<std::pair<std::string, std::string>> outputs;  // (type name, rendered)
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const Record& r = *chosen[i];
        auto items = flatten(r, fo, results[i]);
        const Figure fig = build_figure(r, std::move(items));
        std::string text;
        switch (opt.format) {
        case Format::Svg: text = render_svg(fig, ro); break;
        case Format::Json: text = render_json(fig); break;
        case Format::Table: text = render_table(fig); break;
        case Format::Term:
            text = render_term(fig, ro);
            if (fig.tip)
                text += std::format("\n  tip: ordering fields by descending alignment would shrink this to {} (saves {}).\n",
                                    plural_bytes(fig.tip->new_size), plural_bytes(fig.size - fig.tip->new_size));
            if (fig.approx) text += "  note: some sizes were inferred from offsets and may include padding.\n";
            break;
        }
        outputs.emplace_back(r.name, std::move(text));
    }

    // ---- 7. write -------------------------------------------------------------
    const bool to_dir = opt.output && (outputs.size() > 1 || opt.output->ends_with('/') || fs::is_directory(*opt.output));
    const bool files_by_default = !opt.output && opt.format == Format::Svg && outputs.size() > 1;
    if (to_dir || files_by_default) {
        const fs::path dir = opt.output ? fs::path(*opt.output) : fs::current_path();
        fs::create_directories(dir);
        for (const auto& [name, text] : outputs) {
            const fs::path p = dir / (safe_filename(name) + "." + ext(opt.format));
            if (!write_file(p, text)) die("cannot write " + p.string());
            std::cerr << "wrote " << p.string() << '\n';
        }
    } else if (opt.output) {
        if (!write_file(*opt.output, outputs.front().second)) die("cannot write " + *opt.output);
        std::cerr << "wrote " << *opt.output << '\n';
    } else {
        for (std::size_t i = 0; i < outputs.size(); ++i) std::cout << (i ? "\n" : "") << outputs[i].second;
    }
    return 0;
}
