#include "clang_dump.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace ml {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool consume_suffix(std::string& s, std::string_view suffix) {
    if (s.size() >= suffix.size() && std::string_view(s).substr(s.size() - suffix.size()) == suffix) {
        s.resize(s.size() - suffix.size());
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return true;
    }
    return false;
}

bool is_identifier(std::string_view s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s.front()))) return false;
    return std::ranges::all_of(s, ident_char);
}

bool is_builtin_word(std::string_view s) {
    static constexpr std::string_view words[] = {"int",  "char",   "short",    "long",   "unsigned",
                                                 "signed", "bool", "_Bool",    "float",  "double",
                                                 "char8_t", "char16_t", "char32_t", "wchar_t"};
    return std::ranges::find(words, s) != std::end(words);
}

// Returns the keyword if `s` starts with one ("struct", "class", "union", "enum").
std::string_view leading_keyword(std::string_view s) {
    for (std::string_view kw : {"struct", "class", "union", "enum"}) {
        if (s.size() > kw.size() && s.substr(0, kw.size()) == kw && s[kw.size()] == ' ') return kw;
    }
    return {};
}

std::optional<std::size_t> number_after(std::string_view text, std::string_view key) {
    std::size_t pos = 0;
    while ((pos = text.find(key, pos)) != std::string_view::npos) {
        const bool boundary = pos == 0 || !ident_char(text[pos - 1]);
        if (boundary) {
            std::size_t value = 0;
            const char* b = text.data() + pos + key.size();
            const auto [p, ec] = std::from_chars(b, text.data() + text.size(), value);
            if (ec == std::errc{} && p != b) return value;
        }
        pos += key.size();
    }
    return std::nullopt;
}

void parse_footer(Record& r, std::string_view text) {
    if (auto v = number_after(text, "sizeof=")) r.size = *v;
    if (auto v = number_after(text, "dsize=")) r.dsize = *v;
    if (auto v = number_after(text, "align=")) r.align = *v;
    if (auto v = number_after(text, "nvsize=")) r.nvsize = *v;
    if (auto v = number_after(text, "nvalign=")) r.nvalign = *v;
}

Node parse_node(std::string text) {
    Node n;
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (consume_suffix(text, "(empty)")) n.is_empty = true;

    if (text.starts_with('(') && text.ends_with("pointer)")) {
        n.kind = NodeKind::VPtr;
        n.name = "vptr";
        n.type = text;
        return n;
    }
    for (std::string_view tag : {"(primary base)", "(virtual base)", "(base)"}) {
        if (consume_suffix(text, tag)) {
            n.kind = NodeKind::Base;
            n.is_virtual_base = tag == "(virtual base)";
            n.type = text;
            n.name = strip_keywords(text);
            n.is_record = true;
            return n;
        }
        // "(virtual base) (empty)" style handled by order above; also accept inner tag
    }
    // Field: "<type> <name>"; the name is the last token when it is an identifier.
    const auto sp = text.find_last_of(' ');
    std::string name = sp == std::string::npos ? "" : text.substr(sp + 1);
    if (sp != std::string::npos && is_identifier(name) && !is_builtin_word(name)) {
        n.name = std::move(name);
        n.type = text.substr(0, sp);
    } else {
        n.type = text;
    }
    n.is_record = !leading_keyword(n.type).empty() && leading_keyword(n.type) != "enum";
    return n;
}

}  // namespace

std::string normalize_type_name(std::string_view s) {
    const std::string stripped = strip_keywords(s);
    std::string out;
    for (char c : stripped)
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    return out;
}

std::string strip_keywords(std::string_view s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const bool at_word_start = i == 0 || !ident_char(s[i - 1]);
        if (at_word_start) {
            if (const auto kw = leading_keyword(s.substr(i)); !kw.empty()) {
                i += kw.size() + 1;
                continue;
            }
        }
        out += s[i++];
    }
    return std::string(trim(out));
}

std::vector<Record> parse_record_layouts(std::string_view text) {
    std::vector<Record> out;
    bool expect_header = false;
    bool in_record = false;
    std::vector<Node*> stack;  // stack[k] is the node at depth k+1

    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;

        if (line.starts_with("*** Dumping")) {
            expect_header = true;
            in_record = false;
            stack.clear();
            continue;
        }
        const auto bar = line.find('|');
        if (bar == std::string_view::npos || (!expect_header && !in_record)) continue;

        const std::string_view lhs = trim(line.substr(0, bar));
        std::string_view content = line.substr(bar + 1);
        if (!content.empty() && content.front() == ' ') content.remove_prefix(1);

        if (lhs.empty()) {  // "[sizeof=..." footer lines
            if (in_record) parse_footer(out.back(), content);
            continue;
        }

        // lhs: "<byte>" or "<byte>:<lo>-<hi>"
        std::size_t byte = 0, lo = 0, hi = 0;
        bool bitfield = false;
        {
            const char* b = lhs.data();
            const char* e = lhs.data() + lhs.size();
            auto [p, ec] = std::from_chars(b, e, byte);
            if (ec != std::errc{}) continue;
            if (p != e && *p == ':') {
                auto [p2, ec2] = std::from_chars(p + 1, e, lo);
                if (ec2 != std::errc{} || p2 == e || *p2 != '-') continue;
                auto [p3, ec3] = std::from_chars(p2 + 1, e, hi);
                if (ec3 != std::errc{}) continue;
                bitfield = true;
            }
        }
        const std::size_t indent = content.find_first_not_of(' ');
        if (indent == std::string_view::npos) continue;
        const std::size_t depth = indent / 2;
        std::string body(content.substr(indent));

        if (expect_header && depth == 0) {
            Record r;
            while (!body.empty() && body.back() == ' ') body.pop_back();
            if (consume_suffix(body, "(empty)")) r.is_empty = true;
            const auto kw = leading_keyword(body);
            r.keyword = std::string(kw);
            r.name = strip_keywords(body);
            out.push_back(std::move(r));
            expect_header = false;
            in_record = true;
            stack.clear();
            continue;
        }
        if (!in_record || depth == 0) continue;

        Node n = parse_node(std::move(body));
        n.offset = byte;
        if (bitfield) {
            n.kind = NodeKind::Bitfield;
            n.bit_offset = byte * 8 + lo;
            n.bit_width = hi - lo + 1;
        }
        stack.resize(std::min(stack.size(), depth - 1));
        auto& siblings = stack.empty() ? out.back().children : stack.back()->children;
        siblings.push_back(std::move(n));
        stack.push_back(&siblings.back());
    }
    return out;
}

FindResult find_record(const std::vector<Record>& records, std::string_view wanted) {
    FindResult res;
    const std::string want = normalize_type_name(wanted);
    for (const auto& r : records) {
        if (normalize_type_name(r.name) == want) {
            res.status = FindStatus::Found;
            res.record = &r;
            return res;
        }
    }
    const std::string suffix = "::" + want;
    for (const auto& r : records) {
        const std::string n = normalize_type_name(r.name);
        if (n.size() > suffix.size() && n.ends_with(suffix)) {
            res.candidates.push_back(r.name);
            res.record = &r;
        }
    }
    if (res.candidates.size() == 1) res.status = FindStatus::Found;
    else if (res.candidates.size() > 1) res.status = FindStatus::Ambiguous;
    return res;
}

}  // namespace ml
