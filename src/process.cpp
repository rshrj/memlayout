#include "process.hpp"

#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ml {

std::string shell_quote(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::filesystem::path& p, std::string_view data) {
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

RunResult run(const std::vector<std::string>& argv, const std::filesystem::path& scratch) {
    const auto out_path = scratch / "stdout.txt";
    const auto err_path = scratch / "stderr.txt";
    std::string cmd;
    for (const auto& a : argv) cmd += shell_quote(a) + ' ';
    cmd += "> " + shell_quote(out_path.string()) + " 2> " + shell_quote(err_path.string());

    RunResult r;
    const int status = std::system(cmd.c_str());
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    r.out = read_file(out_path);
    r.err = read_file(err_path);
    return r;
}

std::filesystem::path executable_path() {
    std::error_code ec;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    return std::filesystem::canonical(buf.c_str(), ec);
#else
    return std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
}

TempDir::TempDir() {
    auto tmpl = (std::filesystem::temp_directory_path() / "memlayout-XXXXXX").string();
    if (!mkdtemp(tmpl.data())) throw std::runtime_error("cannot create temporary directory");
    path_ = tmpl;
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

}  // namespace ml
