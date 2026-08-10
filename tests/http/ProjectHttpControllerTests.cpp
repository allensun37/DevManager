#include "application/ProjectManager.h"
#include "application/ProjectService.h"
#include "http/HttpServer.h"
#include "http/ProjectHttpController.h"
#include "query/ProjectQueryEvaluator.h"
#include "repository/ProjectRepository.h"

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

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
    client.set_path_encode(false);
    return client.Get(path);
}

httplib::Result postJson(devmanager::HttpServer& server,
                         const std::string& body) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Post("/api/projects", body, "application/json");
}

httplib::Result putJson(devmanager::HttpServer& server,
                        const std::string& path,
                        const std::string& body) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    return client.Put(path, body, "application/json");
}

class FakeProjectRepository final : public devmanager::ProjectRepository {
public:
    struct CreateCall {
        devmanager::Project project;
        devmanager::ProjectId nextIdAfterCreate;
    };

    explicit FakeProjectRepository(devmanager::ProjectStore initialStore = {})
        : store_(std::move(initialStore)) {}

    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return store_;
    }

    void create(const devmanager::Project& project,
                devmanager::ProjectId nextIdAfterCreate) override {
        createCalls_.push_back(CreateCall{project, nextIdAfterCreate});
        if (failSaves_) {
            throw std::runtime_error("injected save failure");
        }
        store_.projects.push_back(project);
        store_.nextId = nextIdAfterCreate;
    }

    void update(const devmanager::Project& project) override {
        if (failSaves_) {
            throw std::runtime_error("injected save failure");
        }
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [&project](const devmanager::Project& stored) {
                                               return stored.id() == project.id();
                                           });
        if (iterator == store_.projects.end()) {
            throw std::runtime_error("missing project");
        }
        *iterator = project;
    }

    void remove(devmanager::ProjectId id) override {
        if (failSaves_) {
            throw std::runtime_error("injected save failure");
        }
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [id](const devmanager::Project& project) {
                                               return project.id() == id;
                                           });
        if (iterator == store_.projects.end()) {
            throw std::runtime_error("missing project");
        }
        store_.projects.erase(iterator);
    }

    [[nodiscard]] std::optional<devmanager::Project> findById(
        devmanager::ProjectId id) const override {
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [id](const devmanager::Project& project) {
                                               return project.id() == id;
                                           });
        return iterator == store_.projects.end()
                   ? std::nullopt
                   : std::optional<devmanager::Project>{*iterator};
    }

    [[nodiscard]] std::vector<devmanager::Project> query(
        const devmanager::ProjectQuery& projectQuery) const override {
        return devmanager::ProjectQueryEvaluator::query(store_.projects, projectQuery);
    }

    [[nodiscard]] std::uint64_t count(
        const devmanager::ProjectQuery& projectQuery) const override {
        return devmanager::ProjectQueryEvaluator::count(store_.projects, projectQuery);
    }

    void setFailSaves(bool fail) noexcept {
        failSaves_ = fail;
    }

    [[nodiscard]] const std::vector<CreateCall>& createCalls() const noexcept {
        return createCalls_;
    }

private:
    devmanager::ProjectStore store_;
    std::vector<CreateCall> createCalls_;
    bool failSaves_ {false};
};

std::string validProjectJson(const std::string& name = "DevManager") {
    return nlohmann::json{
        {"name", name},
        {"techStack", {"C++", "CMake"}},
        {"description", "project"},
        {"status", "active"},
    }
        .dump();
}

void expectJsonContentType(const httplib::Response& response) {
    EXPECT_EQ(response.get_header_value("Content-Type"),
              "application/json; charset=UTF-8");
}

}  // namespace

TEST(ProjectHttpControllerTest, RegistersProjectRoutesWithoutThrowing) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    httplib::Server server;
    devmanager::ProjectHttpController controller(service);

    EXPECT_NO_THROW(controller.registerRoutes(server));
}

TEST(ProjectHttpControllerTest, AcceptsProjectServiceDependency) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    httplib::Server server;
    devmanager::ProjectHttpController controller(service);

    EXPECT_NO_THROW(controller.registerRoutes(server));
}

TEST(ProjectHttpControllerTest, ListsAllProjectsWhenNoQueryIsPresent) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("Zeta", {"C++"}, "last", "active"), 1U);
    ASSERT_EQ(manager.addProject("Alpha", {"CMake"}, "first", "planned"), 2U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body.at(0).at("name"), "Zeta");
    EXPECT_EQ(body.at(1).at("name"), "Alpha");
    EXPECT_TRUE(response->get_header_value("X-Total-Count").empty());
    EXPECT_TRUE(response->get_header_value("X-Page").empty());
    EXPECT_TRUE(response->get_header_value("X-Page-Size").empty());
}

TEST(ProjectHttpControllerTest, PaginatesWithCompatibleArrayBodyAndHeaders) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("Zeta", {"C++"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Alpha", {"CMake"}, "", "planned"), 2U);
    ASSERT_EQ(manager.addProject("Beta", {"Rust"}, "", "active"), 3U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?name=a&sort=name&page=1&size=1");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 1U);
    EXPECT_EQ(body.at(0).at("name"), "Alpha");
    EXPECT_EQ(response->get_header_value("X-Total-Count"), "3");
    EXPECT_EQ(response->get_header_value("X-Page"), "1");
    EXPECT_EQ(response->get_header_value("X-Page-Size"), "1");
}

TEST(ProjectHttpControllerTest, RejectsMalformedPagingValues) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    for (const std::string& query : {"page=0", "size=0", "size=101", "page=-1",
                                     "page=+1", "page=1.5", "page=", "page=1&page=2",
                                     "page=%201", "page=18446744073709551616"}) {
        const auto response = get(server, "/api/projects?" + query);
        ASSERT_TRUE(response) << query;
        ASSERT_EQ(response->status, 400) << query;
        EXPECT_EQ(nlohmann::json::parse(response->body).at("error").at("code"),
                  "invalid_query")
            << query;
    }
}

TEST(ProjectHttpControllerTest, ReturnsEmptyPagePastEndWithAccurateHeaders) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("Only", {"C++"}, "", "active"), 1U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?page=2&size=1");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    EXPECT_TRUE(nlohmann::json::parse(response->body).empty());
    EXPECT_EQ(response->get_header_value("X-Total-Count"), "1");
    EXPECT_EQ(response->get_header_value("X-Page"), "2");
    EXPECT_EQ(response->get_header_value("X-Page-Size"), "1");
}

TEST(ProjectHttpControllerTest, AllowsSortWithoutAFilter) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("Zeta", {"C++"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Alpha", {"CMake"}, "", "planned"), 2U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?sort=name");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body.at(0).at("name"), "Alpha");
    EXPECT_EQ(body.at(1).at("name"), "Zeta");
}

TEST(ProjectHttpControllerTest, SearchesByNameOrTechnology) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("DevManager", {"C++", "CMake"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Website", {"React"}, "", "planned"), 2U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto nameResponse = get(server, "/api/projects?name=dev");
    ASSERT_TRUE(nameResponse);
    ASSERT_EQ(nameResponse->status, 200);
    expectJsonContentType(*nameResponse);
    const auto nameBody = nlohmann::json::parse(nameResponse->body);
    ASSERT_TRUE(nameBody.is_array());
    ASSERT_EQ(nameBody.size(), 1U);
    EXPECT_EQ(nameBody.at(0).at("name"), "DevManager");

    const auto technologyResponse = get(server, "/api/projects?technology=cpp");
    ASSERT_TRUE(technologyResponse);
    ASSERT_EQ(technologyResponse->status, 200);
    expectJsonContentType(*technologyResponse);
    const auto technologyBody = nlohmann::json::parse(technologyResponse->body);
    ASSERT_TRUE(technologyBody.is_array());
    ASSERT_EQ(technologyBody.size(), 1U);
    EXPECT_EQ(technologyBody.at(0).at("name"), "DevManager");
}

TEST(ProjectHttpControllerTest, FiltersByStatus) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("One", {"C++"}, "", "active"), 1U);
    ASSERT_EQ(manager.addProject("Two", {"CMake"}, "", "planned"), 2U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?status=%20ACTIVE%20");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_TRUE(body.is_array());
    ASSERT_EQ(body.size(), 1U);
    EXPECT_EQ(body.at(0).at("name"), "One");
}

TEST(ProjectHttpControllerTest, RejectsMultipleFilterParameters) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?name=dev&status=active");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 400);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_EQ(body.at("error").at("code"), "invalid_query");
}

TEST(ProjectHttpControllerTest, RejectsUnknownAndRepeatedQueryParameters) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto unknownResponse = get(server, "/api/projects?unknown=value");
    ASSERT_TRUE(unknownResponse);
    ASSERT_EQ(unknownResponse->status, 400);
    expectJsonContentType(*unknownResponse);
    EXPECT_EQ(nlohmann::json::parse(unknownResponse->body).at("error").at("code"),
              "invalid_query");

    const auto emptyKeyResponse = get(server, "/api/projects?=value");
    ASSERT_TRUE(emptyKeyResponse);
    ASSERT_EQ(emptyKeyResponse->status, 400);
    expectJsonContentType(*emptyKeyResponse);
    EXPECT_EQ(nlohmann::json::parse(emptyKeyResponse->body).at("error").at("code"),
              "invalid_query");

    const auto repeatedResponse = get(server, "/api/projects?name=one&name=two");
    ASSERT_TRUE(repeatedResponse);
    ASSERT_EQ(repeatedResponse->status, 400);
    expectJsonContentType(*repeatedResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedResponse->body).at("error").at("code"),
              "invalid_query");

    const auto repeatedSameNameResponse = get(server, "/api/projects?name=one&name=one");
    ASSERT_TRUE(repeatedSameNameResponse);
    ASSERT_EQ(repeatedSameNameResponse->status, 400);
    expectJsonContentType(*repeatedSameNameResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedSameNameResponse->body)
                  .at("error")
                  .at("code"),
              "invalid_query");

    const auto repeatedSortResponse = get(server, "/api/projects?sort=name&sort=name");
    ASSERT_TRUE(repeatedSortResponse);
    ASSERT_EQ(repeatedSortResponse->status, 400);
    expectJsonContentType(*repeatedSortResponse);
    EXPECT_EQ(nlohmann::json::parse(repeatedSortResponse->body).at("error").at("code"),
              "invalid_query");

    const auto emptyValueResponse = get(server, "/api/projects?name=");
    ASSERT_TRUE(emptyValueResponse);
    ASSERT_EQ(emptyValueResponse->status, 400);
    expectJsonContentType(*emptyValueResponse);
    EXPECT_EQ(nlohmann::json::parse(emptyValueResponse->body).at("error").at("code"),
              "invalid_query");

    const auto encodedDelimiterResponse =
        get(server, "/api/projects?name=one%26name=one");
    ASSERT_TRUE(encodedDelimiterResponse);
    ASSERT_EQ(encodedDelimiterResponse->status, 200);
    expectJsonContentType(*encodedDelimiterResponse);
    EXPECT_TRUE(nlohmann::json::parse(encodedDelimiterResponse->body).is_array());
}

TEST(ProjectHttpControllerTest, RejectsInvalidSortKey) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = get(server, "/api/projects?sort=priority");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 400);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    ASSERT_EQ(body.at("error").at("code"), "invalid_query");
}

TEST(ProjectHttpControllerTest, CreatesProjectAndReturns201) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = postJson(server, validProjectJson());

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 201);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("id"), 1U);
    EXPECT_EQ(body.at("name"), "DevManager");
    EXPECT_EQ(body.at("techStack"), nlohmann::json({"C++", "CMake"}));
    EXPECT_EQ(body.at("description"), "project");
    EXPECT_EQ(body.at("status"), "active");
}

TEST(ProjectHttpControllerTest, RejectsMalformedJsonAndInvalidFields) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto malformed = postJson(server, "{not-json");
    ASSERT_TRUE(malformed);
    ASSERT_EQ(malformed->status, 400);
    EXPECT_EQ(nlohmann::json::parse(malformed->body).at("error").at("code"),
              "invalid_json");

    const auto unknown = postJson(
        server,
        R"({"name":"DevManager","techStack":["C++"],"description":"project","status":"active","id":9})");
    ASSERT_TRUE(unknown);
    ASSERT_EQ(unknown->status, 400);
    EXPECT_EQ(nlohmann::json::parse(unknown->body).at("error").at("code"),
              "invalid_request");

    const auto invalidType = postJson(
        server,
        R"({"name":"DevManager","techStack":"C++","description":"project","status":"active"})");
    ASSERT_TRUE(invalidType);
    ASSERT_EQ(invalidType->status, 400);
    EXPECT_EQ(nlohmann::json::parse(invalidType->body).at("error").at("code"),
              "invalid_request");
}

TEST(ProjectHttpControllerTest, UpdatesAllEditableFieldsAndPreservesId) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("Before", {"C++"}, "old", "planned"), 1U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = putJson(
        server,
        "/api/projects/1",
        R"({"name":"After","techStack":["CMake","Ninja"],"description":"new","status":"active"})");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 200);
    expectJsonContentType(*response);
    const auto body = nlohmann::json::parse(response->body);
    EXPECT_EQ(body.at("id"), 1U);
    EXPECT_EQ(body.at("name"), "After");
    EXPECT_EQ(body.at("techStack"), nlohmann::json({"CMake", "Ninja"}));
    EXPECT_EQ(body.at("description"), "new");
    EXPECT_EQ(body.at("status"), "active");
}

TEST(ProjectHttpControllerTest, UpdatesMissingProjectAndReturns404) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = putJson(
        server,
        "/api/projects/99",
        R"({"name":"After","techStack":["CMake"],"description":"new","status":"active"})");

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 404);
    expectJsonContentType(*response);
    EXPECT_EQ(nlohmann::json::parse(response->body).at("error").at("code"),
              "project_not_found");
}

TEST(ProjectHttpControllerTest, DeletesProjectAndReturns204) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    ASSERT_EQ(manager.addProject("DevManager", {"C++"}, "project", "active"), 1U);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Delete("/api/projects/1");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 204);
    EXPECT_TRUE(response->body.empty());
    EXPECT_TRUE(manager.listProjects().empty());
}

TEST(ProjectHttpControllerTest, MapsSaveFailureToPersistenceFailureAndRollsBack) {
    FakeProjectRepository repository;
    repository.setFailSaves(true);
    devmanager::ProjectManager manager(repository);
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto failed = postJson(server, validProjectJson());

    ASSERT_TRUE(failed);
    ASSERT_EQ(failed->status, 500);
    const auto failureBody = nlohmann::json::parse(failed->body);
    EXPECT_EQ(failureBody.at("error").at("code"), "persistence_failure");
    EXPECT_EQ(failureBody.at("error").at("message"), "Persistence operation failed");
    EXPECT_FALSE(failed->get_header_value("X-Request-ID").empty());
    EXPECT_TRUE(manager.listProjects().empty());
    ASSERT_EQ(repository.createCalls().size(), 1U);
    EXPECT_EQ(repository.createCalls().front().project.id(), 1U);
    EXPECT_EQ(repository.createCalls().front().nextIdAfterCreate, 2U);
    EXPECT_TRUE(repository.loadStore().projects.empty());
    EXPECT_EQ(repository.loadStore().nextId, 1U);

    repository.setFailSaves(false);
    const auto retry = postJson(server, validProjectJson("Retry"));
    ASSERT_TRUE(retry);
    ASSERT_EQ(retry->status, 201);
    EXPECT_EQ(nlohmann::json::parse(retry->body).at("id"), 1U);
}

TEST(ProjectHttpControllerTest, MapsIdExhaustionToConflict) {
    constexpr devmanager::ProjectId penultimateId =
        std::numeric_limits<devmanager::ProjectId>::max() - 1U;
    FakeProjectRepository repository(devmanager::ProjectStore{
        std::numeric_limits<devmanager::ProjectId>::max(),
        {devmanager::Project{penultimateId, "Existing", {"C++"}, "", "active"}}});
    devmanager::ProjectManager manager(repository);
    devmanager::ProjectService service(manager);
    devmanager::HttpServer server(service, "127.0.0.1", 0);
    RunningServer running(server);
    ASSERT_TRUE(running.waitUntilReady());

    const auto response = postJson(server, validProjectJson());

    ASSERT_TRUE(response);
    ASSERT_EQ(response->status, 409);
    EXPECT_EQ(nlohmann::json::parse(response->body).at("error").at("code"),
              "id_exhausted");
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().id(), penultimateId);
}
