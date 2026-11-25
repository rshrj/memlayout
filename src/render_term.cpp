#include <algorithm>
#include <array>
#include <format>

#include "render.hpp"

namespace ml {
namespace {

struct Rgb { int r, g, b; };
constexpr std::array<Rgb, 8> kPalette{{{79, 134, 240}, {61, 156, 144}, {217, 138, 47}, {154, 116, 230},
                                       {224, 103, 159}, {109, 186, 76}, {212, 183, 58}, {224, 97, 94}}};
constexpr int kCell = 4;  // columns per byte (3 filled + 1 gap)

Rgb scale(Rgb c, double k) {
    return {static_cast<int>(c.r * k), static_cast<int>(c.g * k), static_cast<int>(c.b * k)};
}

struct Ansi {
    bool on;
    std::string fg(Rgb c) const { return on ? std::format("\x1b[38;2;{};{};{}m", c.r, c.g, c.b) : ""; }
    std::string bg(Rgb c) const { return on ? std::format("\x1b[48;2;{};{};{}m", c.r, c.g, c.b) : ""; }
    std::string reset() const { return on ? "\x1b[0m" : ""; }
    std::string dim() const { return on ? "\x1b[2m" : ""; }
    std::string bold() const { return on ? "\x1b[1m" : ""; }
};

std::string center(std::string text, std::size_t width) {
    if (text.size() > width) text = width > 1 ? text.substr(0, width - 1) + "~" : text.substr(0, width);
    const std::size_t pad = width - text.size();
    return std::string(pad / 2, ' ') + text + std::string(pad - pad / 2, ' ');
}

std::string repeat(std::string_view s, std::size_t n) {
    std::string o;
    for (std::size_t i = 0; i < n; ++i) o += s;
    return o;
}

}  // namespace

std::string render_term(const Figure& f, const RenderOptions& opt) {
    const Ansi ansi{opt.color};
    const std::size_t cols = std::max<std::size_t>(1, std::min<std::size_t>(opt.bytes_per_row, f.size));
    const std::size_t rows = (f.size + cols - 1) / cols;
    constexpr Rgb grey{120, 120, 120};

    std::string out;
    out += std::format("{}{} {}{}  sizeof={} alignof={}\n\n", ansi.bold(), f.keyword, f.name, ansi.reset(), f.size, f.align);

    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t r0 = row * cols, r1 = std::min(r0 + cols, f.size);
        std::string idx = ansi.dim();
        for (std::size_t b = r0; b < r1; ++b) idx += center(std::to_string(b), kCell);
        out += "  " + idx + ansi.reset() + "\n";

        for (std::size_t li = 0; li < f.lanes.size(); ++li) {
            std::string cells = "  ", brackets = "  ", labels = "  ";
            std::size_t cursor = r0;
            for (const Segment& seg : f.lanes[li]) {
                const std::size_t a = std::max(seg.offset, r0), e = std::min(seg.offset + seg.size, r1);
                if (a >= e) continue;
                // blank space for bytes belonging to other lanes (unions)
                const std::string skip = std::string((a - cursor) * kCell, ' ');
                cells += skip; brackets += skip; labels += skip;
                cursor = e;

                const Rgb base = seg.padding ? grey : kPalette[static_cast<std::size_t>(seg.color) % kPalette.size()];
                for (std::size_t b = a; b < e; ++b) {
                    if (seg.padding) {
                        cells += ansi.fg(grey) + "╱╱╱" + ansi.reset() + " ";
                    } else if (opt.color) {
                        cells += ansi.bg(scale(base, 0.8)) + "   " + ansi.reset() + " ";
                    } else {
                        cells += (seg.color % 2 ? "███ " : "▒▒▒ ");
                    }
                }
                const std::size_t w = (e - a) * kCell - 1;
                brackets += ansi.fg(base) + "└" + repeat("─", w - 2) + "┘" + ansi.reset() + " ";
                std::string label = seg.label;
                if (seg.offset < r0) label = "<" + label;
                labels += ansi.fg(base) + center(label, w) + ansi.reset() + " ";
            }
            out += cells + "\n" + brackets + "\n" + labels + "\n";
        }
        out += "\n";
    }
    out += std::format("  {}{}{}\n", ansi.bold(), caption(f), ansi.reset());
    return out;
}

std::string render_table(const Figure& f) {
    struct Row { std::string off, size, align, name, type, note; };
    std::vector<Row> rows;
    for (const auto& it : f.items) {
        Row r;
        if (it.kind == NodeKind::Bitfield) {
            const std::size_t lo = it.bit_offset - it.offset * 8;
            r.off = std::format("{}:{}-{}", it.offset, lo, lo + it.bit_width - 1);
        } else {
            r.off = std::to_string(it.offset);
        }
        r.size = it.zero_size ? "0" : std::to_string(it.size);
        r.align = it.align ? std::to_string(it.align) : "-";
        r.name = it.label;
        r.type = it.type;
        if (it.zero_size) r.note = "empty (0 bytes)";
        else if (it.approx) r.note = "size inferred from offsets";
        else if (it.kind == NodeKind::Bitfield) r.note = std::format("{} bit{}", it.bit_width, it.bit_width == 1 ? "" : "s");
        rows.push_back(std::move(r));
    }
    // Interleave padding runs (taken from the first lane) in offset order.
    std::vector<std::pair<std::size_t, Row>> pads;
    for (const auto& s : f.lanes.front())
        if (s.padding) pads.push_back({s.offset, Row{std::to_string(s.offset), std::to_string(s.size), "-", "(padding)", "", ""}});
    std::vector<Row> merged;
    std::size_t pi = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t off = f.items[i].offset;
        while (pi < pads.size() && pads[pi].first <= off) merged.push_back(pads[pi++].second);
        merged.push_back(rows[i]);
    }
    while (pi < pads.size()) merged.push_back(pads[pi++].second);

    std::array<std::size_t, 5> w{6, 4, 5, 4, 4};
    for (const auto& r : merged) {
        w[0] = std::max(w[0], r.off.size()); w[1] = std::max(w[1], r.size.size());
        w[2] = std::max(w[2], r.align.size()); w[3] = std::max(w[3], r.name.size());
        w[4] = std::max(w[4], r.type.size());
    }
    std::string out = std::format("{} {}: sizeof={}, alignof={}, dsize={}\n", f.keyword, f.name, f.size, f.align, f.dsize);
    auto line = [&](const Row& r) {
        out += std::format("  {:<{}}  {:>{}}  {:>{}}  {:<{}}  {:<{}}  {}\n", r.off, w[0], r.size, w[1], r.align, w[2],
                           r.name, w[3], r.type, w[4], r.note);
    };
    line({"offset", "size", "align", "name", "type", "note"});
    for (const auto& r : merged) line(r);
    out += "\n  " + caption(f) + "\n";
    if (f.tip)
        out += std::format("  tip: ordering fields by descending alignment ({}) would shrink this to {} (saves {}).\n",
                           [&] { std::string s; for (auto& n : f.tip->order) s += (s.empty() ? "" : ", ") + n; return s; }(),
                           plural_bytes(f.tip->new_size), plural_bytes(f.size - f.tip->new_size));
    return out;
}

}  // namespace ml
