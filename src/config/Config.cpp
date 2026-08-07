#include "config/Config.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace devmanager {
namespace {

using Json = nlohmann::json;

[[noreturn]] void throwFieldError(const std::string& field, const std::string& reason) {
    throw std::runtime_error("Invalid configuration field '" + field + "': " + reason);
}

const Json* readObject(const Json& root, const char* field) {
    if (!root.contains(field)) {
        return nullptr;
    }

    const Json& value = root.at(field);
    if (!value.is_object()) {
        throwFieldError(field, "must be an object");
    }
    return &value;
}

std::string readString(const Json& object, const char* field, const std::string& fullField) {
    if (!object.contains(field)) {
        return {};
    }

    const Json& value = object.at(field);
    if (!value.is_string()) {
        throwFieldError(fullField, "must be a string");
    }

    const std::string result = value.get<std::string>();
    if (result.empty()) {
        throwFieldError(fullField, "must not be empty");
    }
    return result;
}

std::filesystem::path readPath(const Json& object,
                               const char* field,
                               const std::string& fullField) {
    const std::string value = readString(object, field, fullField);
    return std::filesystem::path{value};
}

std::uint16_t readPort(const Json& object) {
    constexpr const char* field = "server.port";
    if (!object.contains("port")) {
        return 8080;
    }

    const Json& value = object.at("port");
    if (!value.is_number_integer()) {
        throwFieldError(field, "must be an integer between 1 and 65535");
    }

    std::uint64_t port = 0;
    if (value.is_number_unsigned()) {
        port = value.get<std::uint64_t>();
    } else {
        const std::int64_t signedPort = value.get<std::int64_t>();
        if (signedPort > 0) {
            port = static_cast<std::uint64_t>(signedPort);
        }
    }

    if (port < 1 || port > std::numeric_limits<std::uint16_t>::max()) {
        throwFieldError(field, "must be an integer between 1 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

}  // namespace

Config ConfigLoader::load(const std::filesystem::path& configPath) {
    std::error_code existsError;
    const bool fileExists = std::filesystem::exists(configPath, existsError);
    if (existsError) {
        throw std::runtime_error("Unable to inspect configuration file '" + configPath.string() +
                                 "': " + existsError.message());
    }
    if (!fileExists) {
        return Config{};
    }

    std::ifstream input(configPath, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open configuration file '" + configPath.string() +
                                 "'");
    }

    Json root;
    try {
        input >> root;
    } catch (const Json::exception& error) {
        throw std::runtime_error("Invalid JSON in configuration file '" + configPath.string() +
                                 "': " + error.what());
    }

    if (!root.is_object()) {
        throw std::runtime_error("Invalid configuration root: expected a JSON object");
    }

    Config config;

    if (const Json* server = readObject(root, "server")) {
        if (server->contains("host")) {
            config.server.host = readString(*server, "host", "server.host");
        }
        if (server->contains("port")) {
            config.server.port = readPort(*server);
        }
    }

    if (const Json* storage = readObject(root, "storage")) {
        if (storage->contains("path")) {
            config.storage.path = readPath(*storage, "path", "storage.path");
        }
    }

    if (const Json* logging = readObject(root, "logging")) {
        if (logging->contains("level")) {
            config.logging.level = readString(*logging, "level", "logging.level");
        }
        if (logging->contains("path")) {
            config.logging.path = readPath(*logging, "path", "logging.path");
        }
    }

    return config;
}

}  // namespace devmanager
