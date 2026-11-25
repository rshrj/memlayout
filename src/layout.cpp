#include "layout.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>

#include "clang_dump.hpp"

namespace ml {
namespace {

bool is_identifier(std::string_view s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s.front()))) return false;
    return std::ranges::all_of(s, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

std::string clean_type(std::string_view t) {
    std::string s = strip_keywords(t);
    if (s.starts_with("_Bool")) s.replace(0, 5, "bool");
    return s;
}

// The compiler prints anonymous types as "(anonymous at file:l:c)" / "(unnamed at ...)":
// we cannot spell those in a probe expression.
bool spellable(std::string_view type) {
    return type.find("(anonymous") == std::string_view::npos &&
           type.find("(unnamed") == std::string_view::npos &&
           type.find("(lambda") == std::string_view::npos && type.find('`') == std::string_view::npos;
}

class Flattener {
public:
    Flattener(const Record& rec, const FlattenOptions& opt, std::vector<ProbeRequest>* collect,
              std::span<const ProbeResult> results)
        : rec_(rec), opt_(opt), collect_(collect), results_(results) {}

    std::vector<Item> run() {
        walk(rec_.children, strip_keywords(rec_.name), "", 1, rec_.keyword == "union");
        finalize();
        return std::move(items_);
    }

private:
    ProbeResult request(ProbeRequest req) {
        if (collect_) {
            collect_->push_back(std::move(req));
            return {};
        }
        if (next_ < results_.size()) return results_[next_++];
        return {};
    }

    void walk(const std::vector<Node>& kids, const std::string& tx, const std::string& prefix,
              int level, bool in_union) {
        for (const Node& n : kids) {
            Item it;
            it.kind = n.kind;
            it.is_union_member = in_union;
            it.offset = n.offset;

            switch (n.kind) {
            case NodeKind::Base: {
                const std::string base_tx = strip_keywords(n.type);
                if (n.children.empty()) {
                    it.label = prefix + base_tx + " (base)";
                    it.type = base_tx;
                    it.zero_size = true;
                    items_.push_back(std::move(it));
                } else {
                    // A base class is part of the object, not a nested object: expand it
                    // transparently and qualify the member names with the base name.
                    walk(n.children, base_tx, prefix + base_tx + "::", level, in_union);
                }
                continue;
            }
            case NodeKind::VPtr:
                it.label = prefix + "vptr";
                it.type = "vtable pointer";
                it.size = it.align = opt_.pointer_size;
                break;
            case NodeKind::Bitfield: {
                it.label = prefix + (n.name.empty() ? "<unnamed>" : n.name);
                it.type = clean_type(n.type);
                it.bit_offset = n.bit_offset;
                it.bit_width = n.bit_width;
                it.offset = n.bit_offset / 8;
                it.size = (n.bit_offset + n.bit_width - 1) / 8 - it.offset + 1;
                break;
            }
            case NodeKind::Field: {
                const bool named = is_identifier(n.name);
                const bool expand = n.is_record && named && !n.children.empty() && level < opt_.depth;
                if (expand) {
                    const bool nested_union = n.type.starts_with("union ");
                    walk(n.children, std::format("decltype({}::{})", tx, n.name),
                         prefix + n.name + ".", level + 1, nested_union);
                    continue;
                }
                it.label = prefix + (named ? n.name : "<anonymous>");
                it.type = clean_type(n.type);
                measure(it, n, tx, named);
                break;
            }
            }
            items_.push_back(std::move(it));
        }
    }

    void measure(Item& it, const Node& n, const std::string& tx, bool named) {
        if (n.is_empty) {
            it.zero_size = true;
            return;
        }
        if (n.type.ends_with('&')) {  // references are stored as pointers
            it.size = it.align = opt_.pointer_size;
            return;
        }
        if (n.type.ends_with("[]")) {  // flexible array member
            it.zero_size = true;
            return;
        }
        ProbeRequest req;
        if (named) {
            req = {std::format("sizeof({}::{})", tx, n.name),
                   std::format("alignof(decltype({}::{}))", tx, n.name)};
        } else if (spellable(n.type)) {
            const std::string t = clean_type(n.type);
            req = {std::format("sizeof({})", t), std::format("alignof({})", t)};
        } else {
            return;  // falls back to offset inference
        }
        const ProbeResult r = request(std::move(req));
        it.size = r.size;
        it.align = r.align;
    }

    void finalize() {
        std::ranges::stable_sort(items_, [](const Item& a, const Item& b) {
            if (a.offset != b.offset) return a.offset < b.offset;
            return a.bit_offset < b.bit_offset;
        });
        // Infer sizes we could not measure from the distance to the next item.
        for (auto& it : items_) {
            if (it.size != 0 || it.zero_size) continue;
            std::size_t end = rec_.size;
            for (const auto& other : items_)
                if (other.offset > it.offset) end = std::min(end, other.offset);
            it.size = end > it.offset ? end - it.offset : 0;
            it.approx = true;
        }
    }

    const Record& rec_;
    FlattenOptions opt_;
    std::vector<ProbeRequest>* collect_;
    std::span<const ProbeResult> results_;
    std::size_t next_ = 0;
    std::vector<Item> items_;
};

}  // namespace

std::vector<ProbeRequest> collect_probes(const Record& rec, const FlattenOptions& opt) {
    std::vector<ProbeRequest> reqs;
    Flattener(rec, opt, &reqs, {}).run();
    return reqs;
}

std::vector<Item> flatten(const Record& rec, const FlattenOptions& opt,
                          std::span<const ProbeResult> results) {
    return Flattener(rec, opt, nullptr, results).run();
}

std::string make_probe_main(const std::vector<ProbeRequest>& reqs) {
    std::string s = "extern \"C\" int printf(const char*, ...);\n"
                    "int main() {\n"
                    "  printf(\"%zu %zu\\n\", sizeof(void*), alignof(void*));\n";
    for (const auto& r : reqs)
        s += std::format("  printf(\"%zu %zu\\n\", (std::size_t)({}), (std::size_t)({}));\n",
                         r.size_expr, r.align_expr);
    s += "  return 0;\n}\n";
    return s;
}

}  // namespace ml
