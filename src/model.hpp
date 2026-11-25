#pragma once
// Core data model shared by every stage of the pipeline.
#include <cstddef>
#include <string>
#include <vector>

namespace ml {

enum class NodeKind { Field, Base, VPtr, Bitfield };

// One line of the compiler's record-layout dump, arranged as a tree.
struct Node {
    NodeKind kind = NodeKind::Field;
    std::string name;            // empty for anonymous members
    std::string type;            // exactly as the compiler printed it
    std::size_t offset = 0;      // absolute byte offset inside the outermost record
    std::size_t bit_offset = 0;  // absolute bit offset (bitfields only)
    std::size_t bit_width = 0;   // (bitfields only)
    bool is_empty = false;       // empty class: occupies 0 bytes (EBO / [[no_unique_address]])
    bool is_record = false;      // member whose type is a struct/class/union
    bool is_virtual_base = false;
    std::vector<Node> children;
};

// One top-level `*** Dumping AST Record Layout` block.
struct Record {
    std::string keyword;  // struct | class | union
    std::string name;     // as printed (template arguments included)
    std::size_t size = 0, dsize = 0, align = 0, nvsize = 0, nvalign = 0;
    bool is_empty = false;
    std::vector<Node> children;
};

// A visible, flattened piece of the object: what ends up drawn as a labelled bracket.
struct Item {
    NodeKind kind = NodeKind::Field;
    std::string label;  // path-like name, e.g. "in.c" or "Base::x"
    std::string type;   // cleaned-up type spelling
    std::size_t offset = 0, size = 0, align = 0;
    std::size_t bit_offset = 0, bit_width = 0;
    bool approx = false;  // size inferred from neighbouring offsets, not measured
    bool zero_size = false;
    bool is_union_member = false;
};

}  // namespace ml
