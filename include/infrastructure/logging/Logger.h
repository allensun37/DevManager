#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace devmanager {

class Logger final {
public:
    Logger(std::filesystem::path path, std::string level = "info");
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace devmanager
