#pragma once
// Turns a parsed Record into a flat list of drawable Items, optionally using
// measured sizeof/alignof values obtained by compiling and running a probe.
#include <span>
#include <string>
#include <vector>

#include "model.hpp"

namespace ml {

struct ProbeRequest {
    std::string size_expr;   // C++ expression yielding std::size_t
    std::string align_expr;  // C++ expression yielding std::size_t (or "0")
};

struct ProbeResult {
    std::size_t size = 0;   // 0 means "unavailable" (a real sizeof is never 0)
    std::size_t align = 0;
};

struct FlattenOptions {
    int depth = 1;                  // how many levels of nested struct members to expand
    std::size_t pointer_size = 8;
};

// Pass 1: lists the expressions we want measured (deterministic order).
std::vector<ProbeRequest> collect_probes(const Record& rec, const FlattenOptions& opt);

// Pass 2: builds the items, consuming `results` in the same order as the requests.
// Pass an empty span to skip measuring (sizes are then inferred from offsets).
std::vector<Item> flatten(const Record& rec, const FlattenOptions& opt,
                          std::span<const ProbeResult> results);

// Source text of a probe program body: prints "<size> <align>" per request.
std::string make_probe_main(const std::vector<ProbeRequest>& reqs);

}  // namespace ml
