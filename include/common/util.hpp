#pragma once

#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

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
    // so we go up 4 levels (app -> Release -> build -> project root).
    auto exePath    = fs::read_symlink("/proc/self/exe");
    auto projectDir = exePath.parent_path().parent_path().parent_path().parent_path();
    return (projectDir / relativePath).string();
}

/**
 * @brief Open and parse a JSON config file, printing a consistent error on failure.
 * @param path Path to the JSON file.
 * @return Parsed JSON, or std::nullopt if the file can't be opened or parsed.
 */
[[nodiscard]] inline std::optional<nlohmann::json> loadJsonConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file: " << path << std::endl;
        return std::nullopt;
    }
    try {
        nlohmann::json j;
        file >> j;
        return j;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error: JSON parse error in " << path << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

}  // namespace util
