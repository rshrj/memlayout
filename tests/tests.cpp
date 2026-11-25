// Minimal dependency-free tests for the parser and figure builder.
#include <cstdio>
#include <cstdlib>

#include "clang_dump.hpp"
#include "figure.hpp"
#include "layout.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static const char* kDump = R"(*** Dumping AST Record Layout
         0 | struct Foo
         0 |   char flag
         4 |   int id
         8 |   short energy
           | [sizeof=12, dsize=10, align=4,
           |  nvsize=12, nvalign=4]

*** Dumping AST Record Layout
         0 | struct Bits
     0:0-2 |   unsigned int a
     0:3-7 |   unsigned int b
         4 |   char tail
           | [sizeof=8, dsize=5, align=4,
           |  nvsize=5, nvalign=4]

*** Dumping AST Record Layout
         0 | struct Derived
         0 |   struct Base (primary base)
         0 |     (Base vtable pointer)
         8 |     int x
        12 |   char y
           | [sizeof=16, dsize=13, align=8,
           |  nvsize=13, nvalign=8]
)";

int main() {
    using namespace ml;
    const auto recs = parse_record_layouts(kDump);
    CHECK(recs.size() == 3);
    CHECK(recs[0].name == "Foo" && recs[0].size == 12 && recs[0].dsize == 10 && recs[0].align == 4);
    CHECK(recs[0].children.size() == 3 && recs[0].children[2].name == "energy");
    CHECK(recs[1].children[1].kind == NodeKind::Bitfield && recs[1].children[1].bit_offset == 3 &&
          recs[1].children[1].bit_width == 5);
    CHECK(recs[2].children[0].kind == NodeKind::Base && recs[2].children[0].children[0].kind == NodeKind::VPtr);

    CHECK(find_record(recs, "Foo").status == FindStatus::Found);
    CHECK(find_record(recs, "struct  Foo").record == &recs[0]);
    CHECK(find_record(recs, "Nope").status == FindStatus::NotFound);
    CHECK(normalize_type_name("struct ns::Box<int, 3>") == "ns::Box<int,3>");

    // Without a probe sizes are inferred from offsets: 1 + 3 pad + 4 + 4 -> energy spans 8..12.
    auto items = flatten(recs[0], {}, {});
    CHECK(items.size() == 3);
    CHECK(items[0].size == 4 && items[0].approx);

    // With measured sizes (what the probe provides) the picture is exact.
    const std::vector<ProbeResult> measured{{1, 1}, {4, 4}, {2, 2}};
    const Figure fig = build_figure(recs[0], flatten(recs[0], {}, measured));
    CHECK(fig.data_bytes == 7 && fig.padding_bytes == 5);
    CHECK(caption(fig) == "7 bytes of fields + 5 bytes of padding = 12 bytes");
    CHECK(fig.tip && fig.tip->new_size == 8);  // int, short, char -> 8 bytes

    // Bitfields sharing a byte merge into one segment.
    const Figure bits = build_figure(recs[1], flatten(recs[1], {}, std::vector<ProbeResult>{{1, 1}}));
    CHECK(bits.lanes.size() == 1 && bits.lanes[0][0].size == 1 && bits.lanes[0][0].label == "a:3 b:5");

    std::puts("all tests passed");
}
