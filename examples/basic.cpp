// A tour of layouts. Try:  memlayout examples/basic.cpp            (everything)
//                          memlayout examples/basic.cpp Sample -f svg -o sample.svg
#include <cstdint>
#include <string>

// The figure from the README: 7 bytes of fields + 5 bytes of padding = 12 bytes.
struct Sample {
    char flag;
    int32_t id;
    int16_t energy;
};

// Same members, reordered by descending alignment: no padding in the middle.
struct SampleSorted {
    int32_t id;
    int16_t energy;
    char flag;
};

struct Bits {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 12;
    char tail;
};

struct Shape {
    virtual ~Shape() = default;
    double area_cache;
};

struct Circle : Shape {
    char tag;
    double radius;
};

struct Inner { char c; double d; };
struct Outer {
    char head;
    Inner in;
    std::string name;
    int* ptr;
};

union Value {
    char c;
    int i;
    double d;
    char bytes[10];
};

template <class T, int N> struct Buffer { T data[N]; char used; };
[[maybe_unused]] static Buffer<short, 3> buffer_instance;

int main() {}
