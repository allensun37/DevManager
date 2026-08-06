#include "infrastructure/logging/Logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace devmanager {
namespace {

spdlog::level::level_enum parseLevel(const std::string& level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "info") {
        return spdlog::level::info;
    }
    if (level == "warn" || level == "warning") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }
    if (level == "off") {
        return spdlog::level::off;
    }
    throw std::invalid_argument("Unsupported logging.level: " + level);
}

std::string nextLoggerName() {
    static std::atomic_uint64_t nextId{0};
    return "devmanager-" + std::to_string(nextId++);
}

}  // namespace

class Logger::Impl final {
public:
    Impl(std::filesystem::path path, const std::string& level)
        : name(nextLoggerName()) {
        const spdlog::level::level_enum parsedLevel = parseLevel(level);
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error) {
                throw std::runtime_error("Failed to create log directory: " +
                                         error.message());
            }
        }

        logger = spdlog::basic_logger_mt(name, path.string(), false);
        logger->set_level(parsedLevel);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        logger->flush_on(spdlog::level::info);
    }

    ~Impl() {
        if (logger) {
            spdlog::drop(name);
        }
    }

    std::string name;
    std::shared_ptr<spdlog::logger> logger;
};

Logger::Logger(std::filesystem::path path, std::string level)
    : impl_(std::make_unique<Impl>(std::move(path), level)) {}

Logger::~Logger() = default;

void Logger::info(std::string_view message) {
    impl_->logger->info("{}", std::string(message));
    impl_->logger->flush();
}

void Logger::warn(std::string_view message) {
    impl_->logger->warn("{}", std::string(message));
    impl_->logger->flush();
}

void Logger::error(std::string_view message) {
    impl_->logger->error("{}", std::string(message));
    impl_->logger->flush();
}

}  // namespace devmanager
