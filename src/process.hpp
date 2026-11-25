#pragma once
// Tiny POSIX helpers: run a command, capture its output, manage a scratch directory.
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ml {

struct RunResult {
    int exit_code = -1;
    std::string out;
    std::string err;
};

std::string shell_quote(std::string_view s);
RunResult run(const std::vector<std::string>& argv, const std::filesystem::path& scratch);

std::string read_file(const std::filesystem::path& p);
bool write_file(const std::filesystem::path& p, std::string_view data);

class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace ml
