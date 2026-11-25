#pragma once
// A Figure is the renderer-independent picture of one record: byte-aligned
// segments grouped in lanes (a struct has one lane, a union one lane per member).
#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace ml {

struct Segment {
    std::size_t offset = 0, size = 0;
    bool padding = false;
    std::string label;
    std::string tooltip;
    int color = 0;  // index into a palette (data segments only)
    NodeKind kind = NodeKind::Field;
};

struct ReorderTip {
    std::size_t new_size = 0;
    std::vector<std::string> order;
};

struct Figure {
    std::string keyword, name;
    std::size_t size = 0, align = 0, dsize = 0;
    std::vector<Item> items;
    std::vector<std::vector<Segment>> lanes;
    std::size_t data_bytes = 0, padding_bytes = 0;
    bool approx = false;
    std::optional<ReorderTip> tip;
};

Figure build_figure(const Record& rec, std::vector<Item> items);

// "7 bytes of fields + 5 bytes of padding = 12 bytes"
std::string caption(const Figure& f);
std::string plural_bytes(std::size_t n);

}  // namespace ml
