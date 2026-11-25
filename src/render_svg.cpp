#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "render.hpp"

namespace ml {
namespace {

struct Swatch { const char* stroke; const char* fill; };

constexpr std::array<Swatch, 8> kDark{{
    {"#4f86f0", "#1f2e50"}, {"#3d9c90", "#1f3430"}, {"#d98a2f", "#45301c"}, {"#9a74e6", "#2d2548"},
    {"#e0679f", "#46233a"}, {"#6dba4c", "#26381f"}, {"#d4b73a", "#3d3519"}, {"#e0615e", "#452524"}}};
constexpr std::array<Swatch, 8> kLight{{
    {"#2f6fe0", "#dbe7ff"}, {"#1f8a7c", "#d4f1ec"}, {"#c9741a", "#ffe8cf"}, {"#7a52d6", "#e9e0ff"},
    {"#cc3f83", "#ffdcec"}, {"#4a9a2c", "#def3d3"}, {"#b39512", "#fff4c2"}, {"#cc3c39", "#ffdcdb"}}};

struct Colors {
    const char *bg, *border, *index, *caption, *title, *pad_stroke, *pad_fill, *pad_text, *hatch, *note;
    const std::array<Swatch, 8>* palette;
};
constexpr Colors kDarkTheme{"#111111", "#2a2a2a", "#a8a8a8", "#f2f2f2", "#8a8a8a", "#6e6e6e",
                            "#1c1c1c", "#8c8c8c", "#3a3a3a", "#8a8a8a", &kDark};
constexpr Colors kLightTheme{"#ffffff", "#dddddd", "#666666", "#111111", "#777777", "#a0a0a0",
                             "#f1f1f1", "#777777", "#c8c8c8", "#777777", &kLight};

std::string esc(std::string_view s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

double text_width(std::size_t chars, double fs) { return 0.58 * fs * static_cast<double>(chars); }

// Shrinks / truncates `text` so it fits in `width`.
std::pair<std::string, double> fit(std::string text, double width, double max_fs, double min_fs) {
    double fs = max_fs;
    while (text_width(text.size(), fs) > width && fs > min_fs) fs -= 1;
    if (text_width(text.size(), fs) > width) {
        const auto keep = static_cast<std::size_t>(std::max(1.0, width / (0.58 * fs) - 1));
        if (keep < text.size()) text = text.substr(0, keep) + "…";
    }
    return {std::move(text), fs};
}

}  // namespace

std::string render_svg(const Figure& f, const RenderOptions& opt) {
    const Colors& th = opt.theme == Theme::Dark ? kDarkTheme : kLightTheme;
    constexpr double cell_w = 56, cell_h = 68, gap = 6, pitch = cell_w + gap, margin = 34;
    constexpr double cap_fs = 22;
    const int cols = static_cast<int>(std::max<std::size_t>(1, std::min<std::size_t>(opt.bytes_per_row, f.size)));
    const int rows = static_cast<int>((f.size + cols - 1) / static_cast<std::size_t>(cols));
    const int lanes = static_cast<int>(f.lanes.size());

    const std::string cap = caption(f);
    const std::string heading = std::format("{} {}", f.keyword, f.name);
    const std::string sub = std::format("sizeof = {}  ·  alignof = {}{}", f.size, f.align,
                                        f.approx ? "  ·  some sizes inferred" : "");
    const double content_w = cols * pitch - gap;
    const double width = std::max({content_w, text_width(cap.size(), cap_fs),
                                   text_width(heading.size() + sub.size() + 6, 15)}) + 2 * margin;
    const double left = (width - content_w) / 2;

    const double lane_h = cell_h + 10 + 10 + 38;  // cells, gap, bracket, label
    const double block_h = 28 + lanes * lane_h + (lanes - 1) * 8;
    const double title_h = opt.title ? 40 : 0;
    const double height = margin + title_h + rows * block_h + (rows - 1) * 12 + 18 + cap_fs + margin - 8;

    std::string s;
    s += std::format(
        R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {:.0f} {:.0f}" width="{:.0f}" height="{:.0f}" role="img" aria-label="{}">)"
        "\n",
        width, height, width, height, esc(f.keyword + " " + f.name + " memory layout: " + cap));
    s += R"(<style>text{font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Helvetica,Arial,sans-serif}</style>)"
         "\n";
    s += std::format(R"(<rect x="1" y="1" width="{:.0f}" height="{:.0f}" rx="26" fill="{}" stroke="{}" stroke-width="2"/>)" "\n",
                     width - 2, height - 2, th.bg, th.border);

    double y = margin - 4;
    if (opt.title) {
        s += std::format(R"(<text x="{:.1f}" y="{:.1f}" font-size="15" fill="{}"><tspan font-weight="600" fill="{}">{}</tspan>  {}</text>)" "\n",
                         left, y + 12, th.title, th.caption, esc(heading), esc(sub));
        y += title_h;
    }

    for (int row = 0; row < rows; ++row) {
        const std::size_t r0 = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols);
        const std::size_t r1 = std::min(r0 + static_cast<std::size_t>(cols), f.size);
        for (std::size_t b = r0; b < r1; ++b) {
            s += std::format(R"(<text x="{:.1f}" y="{:.1f}" font-size="24" fill="{}" text-anchor="middle">{}</text>)" "\n",
                             left + static_cast<double>(b - r0) * pitch + cell_w / 2, y + 20, th.index, b);
        }
        double ly = y + 28;
        for (const auto& lane : f.lanes) {
            for (const Segment& seg : lane) {
                const std::size_t a = std::max(seg.offset, r0);
                const std::size_t e = std::min(seg.offset + seg.size, r1);
                if (a >= e) continue;
                const Swatch sw = (*th.palette)[static_cast<std::size_t>(seg.color) % th.palette->size()];
                const char* stroke = seg.padding ? th.pad_stroke : sw.stroke;
                const char* fill = seg.padding ? th.pad_fill : sw.fill;
                s += std::format("<g><title>{}</title>\n", esc(seg.tooltip));
                for (std::size_t b = a; b < e; ++b) {
                    const double x = left + static_cast<double>(b - r0) * pitch;
                    s += std::format(R"(<rect x="{:.1f}" y="{:.1f}" width="{}" height="{}" rx="9" fill="{}" stroke="{}" stroke-width="3"/>)" "\n",
                                     x, ly, cell_w, cell_h, fill, stroke);
                    if (seg.padding) {
                        s += std::format(R"(<path d="M{:.1f} {:.1f}L{:.1f} {:.1f}M{:.1f} {:.1f}L{:.1f} {:.1f}" stroke="{}" stroke-width="3" stroke-linecap="round"/>)" "\n",
                                         x + 14, ly + 46, x + 42, ly + 16, x + 14, ly + 60, x + 50, ly + 24, th.hatch);
                    }
                }
                const double x1 = left + static_cast<double>(a - r0) * pitch;
                const double x2 = left + static_cast<double>(e - r0) * pitch - gap;
                const double by = ly + cell_h + 12;
                s += std::format(R"(<path d="M{:.1f} {:.1f}V{:.1f}H{:.1f}V{:.1f}" fill="none" stroke="{}" stroke-width="4"/>)" "\n",
                                 x1, by, by + 10, x2, by, stroke);
                std::string label = seg.label;
                if (seg.offset < r0) label = "↪ " + label;
                const auto [txt, fs] = fit(std::move(label), x2 - x1 + gap, 26, 11);
                s += std::format(R"(<text x="{:.1f}" y="{:.1f}" font-size="{:.0f}" fill="{}" text-anchor="middle">{}</text>)" "\n",
                                 (x1 + x2) / 2, by + 10 + 6 + fs, fs, seg.padding ? th.pad_text : sw.stroke, esc(txt));
                s += "</g>\n";
            }
            ly += lane_h + 8;
        }
        y += block_h + 12;
    }
    s += std::format(R"(<text x="{:.1f}" y="{:.1f}" font-size="{}" fill="{}" text-anchor="middle">{}</text>)" "\n",
                     width / 2, height - margin - 2, cap_fs, th.caption, esc(cap));
    s += "</svg>\n";
    return s;
}

}  // namespace ml
