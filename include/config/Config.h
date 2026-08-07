#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace devmanager {

struct ServerConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
};

struct StorageConfig {
    std::filesystem::path path{"data/projects.json"};
};

struct LoggingConfig {
    std::string level{"info"};
    std::filesystem::path path{"logs/devmanager.log"};
};

struct Config {
    ServerConfig server;
    StorageConfig storage;
    LoggingConfig logging;
};

class ConfigLoader final {
public:
    [[nodiscard]] static Config load(const std::filesystem::path& configPath);
};

}  // namespace devmanager
