#include "application/ProjectManager.h"
#include "application/ProjectService.h"
#include "DevManagerVersion.h"
#include "http/HttpServer.h"
#include "infrastructure/logging/Logger.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

std::filesystem::path makeLoggerTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("devmanager-http-logger-tests-" + std::to_string(timestamp) +
                            "-" + std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string readLoggerFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

class RunningServer final {
public:
    explicit RunningServer(devmanager::HttpServer& server) : server_(server) {
        server_.bind();
        thread_ = std::thread([this]() { server_.run(); });
    }

    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    RunningServer(const RunningServer&) = delete;
    RunningServer& operator=(const RunningServer&) = delete;

    bool waitUntilReady() const {
        httplib::Client client("127.0.0.1", static_cast<int>(server_.boundPort()));
        client.set_connection_timeout(0, 100000);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline) {
            if (client.Get("/api/projects")) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

private:
    devmanager::HttpServer& server_;
    std::thread thread_;
};

httplib::Result get(devmanager::HttpServer& server, const std::string& path) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Get(path);
}

httplib::Result postJson(devmanager::HttpServer& server,
                         const std::string& body) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Post("/api/projects", body, "application/json");
}

bool isValidRequestId(const std::string& value) {
    if (value.empty() || value.size() > 64U) {
        return false;
    }
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '_' || character == '-') {
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

TEST(HttpServerIntegrationTest, BindsDynamicPortAndStopsCleanly) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);

    EXPECT_GT(server.boundPort(), 0U);
    EXPECT_TRUE(running.waitUntilReady());
}

TEST(HttpServerIntegrationTest, EmptyProjectListReturnsJsonArray) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    EXPECT_EQ(nlohmann::json::parse(response->body), nlohmann::json::array());
}

TEST(HttpServerIntegrationTest, HealthReturnsOkAndPropagatesRequestId) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    httplib::Headers headers{{"X-Request-ID", "client.req-01"}};
    const auto response = client.Get("/health", headers);

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body, (nlohmann::json{{"status", "ok"}}));
    EXPECT_EQ(response->get_header_value("X-Request-ID"), "client.req-01");
}

TEST(HttpServerIntegrationTest, InfoReturnsGeneratedVersion) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/info");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("name"), "DevManager");
    EXPECT_EQ(body.at("version"), devmanager::kDevManagerVersion);
    EXPECT_TRUE(isValidRequestId(response->get_header_value("X-Request-ID")));
}

TEST(HttpServerIntegrationTest, StatisticsReturnsNormalizedProjectCounts) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("One", {" C++ ", "c++", "CMake"}, "", " Active "), 1U);
    ASSERT_EQ(manager.addProject("Two", {"CMAKE", "Rust"}, "", "active"), 2U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/statistics");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 200);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("totalProjects"), 2U);
    EXPECT_EQ(body.at("status").at("active"), 2U);
    EXPECT_EQ(body.at("technology").at("c++"), 1U);
    EXPECT_EQ(body.at("technology").at("cmake"), 2U);
    EXPECT_EQ(body.at("technology").at("rust"), 1U);
    EXPECT_TRUE(isValidRequestId(response->get_header_value("X-Request-ID")));
}

TEST(HttpServerIntegrationTest, MissingOrInvalidRequestIdIsReplacedForSuccessAndNotFound) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    httplib::Headers invalid{{"X-Request-ID", "bad id with spaces"}};
    const auto success = client.Get("/health", invalid);
    ASSERT_TRUE(success);
    ASSERT_EQ(success->status, 200);
    EXPECT_TRUE(isValidRequestId(success->get_header_value("X-Request-ID")));
    EXPECT_NE(success->get_header_value("X-Request-ID"), "bad id with spaces");

    const auto notFound = client.Get("/missing");
    ASSERT_TRUE(notFound);
    EXPECT_EQ(notFound->status, 404);
    EXPECT_TRUE(isValidRequestId(notFound->get_header_value("X-Request-ID")));
}

TEST(HttpServerIntegrationTest, DeleteMissingProjectReturnsNotFoundError) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/99");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 404);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_EQ(body.at("error").at("code"), "project_not_found");
}

TEST(HttpServerIntegrationTest, DeleteInvalidProjectIdReturnsBadRequest) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/not-an-id");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 400);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.contains("error"));
    EXPECT_EQ(body.at("error").at("code"), "invalid_id");
}

TEST(HttpServerIntegrationTest, ConcurrentCreatesProduceUniqueIds) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    constexpr std::size_t requestCount = 12U;
    const std::string body = nlohmann::json{
        {"name", "DevManager"},
        {"techStack", {"C++", "CMake"}},
        {"description", "project"},
        {"status", "active"},
    }
        .dump();
    std::vector<int> statuses(requestCount, 0);
    std::vector<devmanager::ProjectId> ids(requestCount, 0U);
    std::vector<std::thread> workers;
    workers.reserve(requestCount);
    for (std::size_t index = 0; index < requestCount; ++index) {
        workers.emplace_back([&, index]() {
            const auto response = postJson(server, body);
            if (response) {
                statuses[index] = response->status;
                if (response->status == 201) {
                    ids[index] = nlohmann::json::parse(response->body)
                                     .at("id")
                                     .get<devmanager::ProjectId>();
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    for (const int status : statuses) {
        EXPECT_EQ(status, 201);
    }
    std::sort(ids.begin(), ids.end());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        EXPECT_EQ(ids[index], static_cast<devmanager::ProjectId>(index + 1U));
    }
    EXPECT_EQ(manager.listProjects().size(), requestCount);
}

TEST(HttpServerIntegrationTest, LogsStartupAndHttpErrorsThroughInjectedLogger) {
    const std::filesystem::path directory = makeLoggerTestDirectory();
    const std::filesystem::path logPath = directory / "devmanager.log";
    {
        devmanager::Logger logger(logPath, "info");
        devmanager::ProjectManager manager;
        devmanager::ProjectService service(manager);
        devmanager::HttpServer server(service, logger, "127.0.0.1", 0);
        RunningServer running(server);
        ASSERT_TRUE(running.waitUntilReady());

        httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
        httplib::Headers headers{{"X-Request-ID", "logged.req"}};
        const auto response = client.Get("/unknown", headers);
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, 404);
        EXPECT_EQ(response->get_header_value("X-Request-ID"), "logged.req");
    }

    const std::string contents = readLoggerFile(logPath);
    EXPECT_NE(contents.find("HTTP server started"), std::string::npos);
    EXPECT_NE(contents.find("HTTP error"), std::string::npos);
    EXPECT_NE(contents.find("status=404"), std::string::npos);
    EXPECT_NE(contents.find("request_id=logged.req"), std::string::npos);

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    ASSERT_FALSE(error) << "Failed to remove logger test directory: " << error.message();
}
