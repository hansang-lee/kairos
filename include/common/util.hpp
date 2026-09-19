#pragma once

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
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
        if (f)
            f();
    }
};

/**
 * @brief Format a Unix timestamp (seconds) to "YYYY-MM-DD" string.
 * @param timestamp Unix epoch seconds.
 * @return Formatted date string.
 */
[[nodiscard]] inline std::string formatTime(const int64_t timestamp) {
    const std::time_t  t  = static_cast<std::time_t>(timestamp);
    const std::tm*     lt = std::localtime(&t);
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
    const std::time_t  t  = static_cast<std::time_t>(timestamp);
    const std::tm*     lt = std::localtime(&t);
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
 * @brief Look up a key from a .env file, falling back to the process environment.
 *
 * The file is read once per path and cached, so this is cheap to call repeatedly.
 * Returns an empty string when the key is absent or blank, which callers treat as
 * "feature not configured" rather than an error.
 */
[[nodiscard]] inline std::string envValue(const std::string& key, const std::string& envPath = ".env") {
    static std::map<std::string, std::map<std::string, std::string>> cache;

    auto it = cache.find(envPath);
    if (it == cache.end()) {
        std::map<std::string, std::string> values;
        std::ifstream                      file(envPath);
        std::string                        line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const auto pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            auto trim = [](std::string s) {
                const auto first = s.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) {
                    return std::string();
                }
                return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
            };
            values[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
        }
        it = cache.emplace(envPath, std::move(values)).first;
    }

    if (const auto v = it->second.find(key); v != it->second.end() && !v->second.empty()) {
        return v->second;
    }
    if (const char* v = std::getenv(key.c_str()); v && *v) {
        return v;
    }
    return "";
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
