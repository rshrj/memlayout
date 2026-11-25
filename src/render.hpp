#pragma once
#include <string>

#include "figure.hpp"

namespace ml {

enum class Theme { Dark, Light };

struct RenderOptions {
    int bytes_per_row = 16;
    Theme theme = Theme::Dark;
    bool color = false;   // terminal output only
    bool title = true;
};

std::string render_svg(const Figure& f, const RenderOptions& opt);
std::string render_term(const Figure& f, const RenderOptions& opt);  // boxes + legend
std::string render_table(const Figure& f);                           // plain text table
std::string render_json(const Figure& f);

}  // namespace ml
