#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model.hpp"

namespace ml {

// Parses the output of `clang -Xclang -fdump-record-layouts-complete`.
std::vector<Record> parse_record_layouts(std::string_view dump);

// "struct ns::Box<int, 3>" -> "ns::Box<int,3>" (keywords and whitespace removed)
std::string normalize_type_name(std::string_view s);
// "struct ns::Box<int, 3>" -> "ns::Box<int, 3>"
std::string strip_keywords(std::string_view s);

enum class FindStatus { Found, NotFound, Ambiguous };
struct FindResult {
    FindStatus status = FindStatus::NotFound;
    const Record* record = nullptr;
    std::vector<std::string> candidates;
};
// Exact (normalised) match first, then unique "::name" suffix match.
FindResult find_record(const std::vector<Record>& records, std::string_view wanted);

}  // namespace ml
