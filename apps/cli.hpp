#pragma once

// Tiny command line helper shared by the executables: --key value pairs and
// bare --flags.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace mml::cli {

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (key.rfind("--", 0) != 0) {
                continue;
            }
            key = key.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                values_[key] = argv[++i];
            } else {
                values_[key] = "1";
            }
        }
    }

    [[nodiscard]] bool has(const std::string& key) const { return values_.count(key) > 0; }

    [[nodiscard]] long long get_int(const std::string& key, long long fallback) const {
        auto it = values_.find(key);
        return it == values_.end() ? fallback : std::atoll(it->second.c_str());
    }

    [[nodiscard]] double get_double(const std::string& key, double fallback) const {
        auto it = values_.find(key);
        return it == values_.end() ? fallback : std::atof(it->second.c_str());
    }

    [[nodiscard]] std::string get_string(const std::string& key, const std::string& fallback) const {
        auto it = values_.find(key);
        return it == values_.end() ? fallback : it->second;
    }

private:
    std::map<std::string, std::string> values_;
};

inline std::ofstream open_csv(const std::string& dir, const std::string& name) {
    std::filesystem::create_directories(dir);
    const auto path = std::filesystem::path(dir) / name;
    std::ofstream out(path);
    if (!out) {
        std::cerr << "warning: could not open " << path << " for writing\n";
    }
    return out;
}

}  // namespace mml::cli
