#include <format>

#include "render.hpp"

namespace ml {
namespace {
std::string q(std::string_view s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) o += std::format("\\u{:04x}", static_cast<int>(c));
            else o += c;
        }
    }
    return o + '"';
}
const char* kind_name(NodeKind k) {
    switch (k) {
    case NodeKind::Field: return "field";
    case NodeKind::Base: return "base";
    case NodeKind::VPtr: return "vptr";
    case NodeKind::Bitfield: return "bitfield";
    }
    return "field";
}
}  // namespace

std::string render_json(const Figure& f) {
    std::string o = std::format(
        "{{\n  \"kind\": {}, \"name\": {}, \"size\": {}, \"align\": {}, \"dsize\": {},\n"
        "  \"field_bytes\": {}, \"padding_bytes\": {}, \"inferred\": {},\n  \"items\": [\n",
        q(f.keyword), q(f.name), f.size, f.align, f.dsize, f.data_bytes, f.padding_bytes, f.approx);
    for (std::size_t i = 0; i < f.items.size(); ++i) {
        const Item& it = f.items[i];
        o += std::format("    {{\"kind\": {}, \"name\": {}, \"type\": {}, \"offset\": {}, \"size\": {}, \"align\": {}",
                         q(kind_name(it.kind)), q(it.label), q(it.type), it.offset, it.size, it.align);
        if (it.kind == NodeKind::Bitfield) o += std::format(", \"bit_offset\": {}, \"bit_width\": {}", it.bit_offset, it.bit_width);
        o += std::format(", \"inferred\": {}}}{}\n", it.approx, i + 1 < f.items.size() ? "," : "");
    }
    o += "  ],\n  \"padding\": [";
    bool first = true;
    for (const auto& s : f.lanes.front()) {
        if (!s.padding) continue;
        o += std::format("{}{{\"offset\": {}, \"size\": {}}}", first ? "" : ", ", s.offset, s.size);
        first = false;
    }
    o += "]";
    if (f.tip) {
        o += std::format(",\n  \"reorder_tip\": {{\"new_size\": {}, \"order\": [", f.tip->new_size);
        for (std::size_t i = 0; i < f.tip->order.size(); ++i) o += std::format("{}{}", i ? ", " : "", q(f.tip->order[i]));
        o += "]}";
    }
    return o + "\n}\n";
}

}  // namespace ml
