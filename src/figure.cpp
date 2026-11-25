#include "figure.hpp"

#include <algorithm>
#include <format>
#include <numeric>

namespace ml {
namespace {

std::size_t round_up(std::size_t v, std::size_t a) { return a ? (v + a - 1) / a * a : v; }

std::optional<ReorderTip> make_tip(const Record& rec, const std::vector<Item>& items) {
    if (rec.keyword == "union" || items.size() < 2) return std::nullopt;
    for (const auto& it : items) {
        // Only plain top-level fields: reordering bases, vptrs, bitfields or nested
        // members would change semantics or is not a simple permutation.
        if (it.kind != NodeKind::Field || it.approx || it.zero_size || it.align == 0 || it.size == 0 ||
            it.label.find('.') != std::string::npos || it.label.find("::") != std::string::npos)
            return std::nullopt;
    }
    std::vector<const Item*> order;
    for (const auto& it : items) order.push_back(&it);
    std::ranges::stable_sort(order, [](const Item* a, const Item* b) { return a->align > b->align; });
    std::size_t off = 0, max_align = rec.align ? rec.align : 1;
    for (const Item* it : order) {
        off = round_up(off, it->align) + it->size;
        max_align = std::max(max_align, it->align);
    }
    const std::size_t total = round_up(off, max_align);
    if (total >= rec.size) return std::nullopt;
    ReorderTip tip;
    tip.new_size = total;
    for (const Item* it : order) tip.order.push_back(it->label);
    return tip;
}

}  // namespace

std::string plural_bytes(std::size_t n) { return std::format("{} byte{}", n, n == 1 ? "" : "s"); }

std::string caption(const Figure& f) {
    return std::format("{} of fields + {} of padding = {}", plural_bytes(f.data_bytes),
                       plural_bytes(f.padding_bytes), plural_bytes(f.size));
}

Figure build_figure(const Record& rec, std::vector<Item> items) {
    Figure f;
    f.keyword = rec.keyword;
    f.name = rec.name;
    f.size = rec.size;
    f.align = rec.align;
    f.dsize = rec.dsize;
    f.tip = make_tip(rec, items);

    // 1. Data segments (consecutive bitfields that share bytes become one segment).
    std::vector<Segment> segs;
    int color = 0;
    for (const auto& it : items) {
        f.approx |= it.approx;
        if (it.zero_size || it.size == 0) continue;
        const std::string detail =
            it.kind == NodeKind::Bitfield
                ? std::format("{} : {} bit{} at bit {}", it.type, it.bit_width, it.bit_width == 1 ? "" : "s", it.bit_offset)
                : std::format("{}, align {}", it.type, it.align ? std::to_string(it.align) : "?");
        const std::string label = it.kind == NodeKind::Bitfield ? std::format("{}:{}", it.label, it.bit_width) : it.label;
        const std::string tip = std::format("{} — {}\noffset {}, {}{}", it.label, detail, it.offset,
                                            plural_bytes(it.size), it.approx ? " (inferred)" : "");
        if (it.kind == NodeKind::Bitfield && !segs.empty() && segs.back().kind == NodeKind::Bitfield &&
            it.offset < segs.back().offset + segs.back().size) {
            Segment& s = segs.back();
            s.size = std::max(s.offset + s.size, it.offset + it.size) - s.offset;
            s.label += " " + label;
            s.tooltip += "\n" + tip;
            continue;
        }
        Segment s;
        s.offset = it.offset;
        s.size = it.size;
        s.label = label;
        s.tooltip = tip;
        s.color = color++;
        s.kind = it.kind;
        segs.push_back(std::move(s));
    }

    // 2. Byte coverage -> padding.
    std::vector<bool> covered(f.size, false);
    for (const auto& s : segs)
        for (std::size_t b = s.offset; b < std::min(s.offset + s.size, f.size); ++b) covered[b] = true;
    f.data_bytes = static_cast<std::size_t>(std::ranges::count(covered, true));
    f.padding_bytes = f.size - f.data_bytes;

    std::vector<Segment> pads;
    for (std::size_t b = 0; b < f.size;) {
        if (covered[b]) { ++b; continue; }
        std::size_t e = b;
        while (e < f.size && !covered[e]) ++e;
        Segment p;
        p.offset = b;
        p.size = e - b;
        p.padding = true;
        p.label = "padding";
        p.tooltip = std::format("{} of padding at offset {}", plural_bytes(p.size), p.offset);
        pads.push_back(std::move(p));
        b = e;
    }

    // 3. Lanes: greedy interval colouring so overlapping members (unions) stack up.
    std::vector<std::vector<Segment>> lanes;
    for (auto& s : segs) {
        auto lane = std::ranges::find_if(lanes, [&](const auto& l) {
            return l.back().offset + l.back().size <= s.offset;
        });
        if (lane == lanes.end()) {
            lanes.emplace_back();
            lane = std::prev(lanes.end());
        }
        lane->push_back(std::move(s));
    }
    if (lanes.empty()) lanes.emplace_back();
    for (auto& l : lanes) {
        l.insert(l.end(), pads.begin(), pads.end());
        std::ranges::sort(l, {}, &Segment::offset);
    }
    f.lanes = std::move(lanes);
    f.items = std::move(items);
    return f;
}

}  // namespace ml
