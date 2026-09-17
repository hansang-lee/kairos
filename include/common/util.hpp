#pragma once

#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>

namespace util {

/**
 * @brief RAII scope guard — executes a function on destruction.
 *
 * Usage:
 *   yFinance::init();
 *   Defer cleanup([&] { yFinance::close(); });
 */
struct Defer {
    std::function<void()> f;
    explicit Defer(std::function<void()> f)
        : f(std::move(f)) {}
    ~Defer() {
        if (f) f();
    }
};

/**
 * @brief Format a Unix timestamp (seconds) to "YYYY-MM-DD" string.
 * @param timestamp Unix epoch seconds.
 * @return Formatted date string.
 */
[[nodiscard]] inline std::string formatTime(const int64_t timestamp) {
    const std::time_t t = static_cast<std::time_t>(timestamp);
    const std::tm*    lt = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(lt, "%Y-%m-%d");
    return oss.str();
}

/**
 * @brief Format a Unix timestamp to "YYYY-MM-DD HH:MM:SS" string.
 * @param timestamp Unix epoch seconds.
 * @return Formatted datetime string.
 */
[[nodiscard]] inline std::string formatDateTime(const int64_t timestamp) {
    const std::time_t t = static_cast<std::time_t>(timestamp);
    const std::tm*    lt = std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(lt, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

/**
 * @brief Resolve a relative path from the executable's directory.
 *
 * Useful for finding config files when running from build/Release/app/:
 *   resolveFromExe("config/portfolio.json") → /project/root/config/portfolio.json
 *
 * @param relativePath Path relative to the project root.
 * @return Absolute path resolved from the executable's location.
 */
[[nodiscard]] inline std::string resolveFromExe(const std::string& relativePath) {
    namespace fs = std::filesystem;
    // The executable is typically at build/Release/app/<name>,
    // so we go up 3 levels to get the project root.
    auto exePath    = fs::read_symlink("/proc/self/exe");
    auto projectDir = exePath.parent_path().parent_path().parent_path();
    return (projectDir / relativePath).string();
}

}  // namespace util
