#include "application/ProjectManager.h"
#include "application/ProjectService.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

TEST(ProjectServiceTest, ExposesValueReturningCrudAndQueryOperations) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);

    const devmanager::Project created =
        service.addProject("DevManager", {"C++", "CMake"}, "project", "active");
    EXPECT_EQ(created.id(), 1U);
    EXPECT_EQ(created.name(), "DevManager");

    const std::vector<devmanager::Project> listed = service.listProjects();
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed.front().id(), created.id());

    const std::optional<devmanager::Project> updated =
        service.updateProject(1, "Renamed", {"CMake"}, "updated", "done");
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->id(), created.id());
    EXPECT_EQ(updated->name(), "Renamed");

    EXPECT_EQ(service.searchByName("renamed").size(), 1U);
    EXPECT_EQ(service.searchByTechnology("cmake").size(), 1U);
    EXPECT_EQ(service.filterByStatus(" DONE ").size(), 1U);
    EXPECT_TRUE(service.deleteProject(1));
    EXPECT_TRUE(service.listProjects().empty());
}

TEST(ProjectServiceTest, StatisticsNormalizeKeysAndCountDuplicateTechnologyOncePerProject) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);

    static_cast<void>(service.addProject("One", {" C++ ", "c++", "CMake"}, "", " Active "));
    static_cast<void>(service.addProject("Two", {"CMAKE", "Rust"}, "", "active"));

    const devmanager::ProjectStatistics statistics = service.statistics();
    EXPECT_EQ(statistics.totalProjects, 2U);
    EXPECT_EQ(statistics.status.at("active"), 2U);
    EXPECT_EQ(statistics.technology.at("c++"), 1U);
    EXPECT_EQ(statistics.technology.at("cmake"), 2U);
    EXPECT_EQ(statistics.technology.at("rust"), 1U);
}

TEST(ProjectServiceTest, StatisticsForEmptyServiceIsEmpty) {
    devmanager::ProjectManager manager;
    const devmanager::ProjectService service(manager);

    const devmanager::ProjectStatistics statistics = service.statistics();
    EXPECT_EQ(statistics.totalProjects, 0U);
    EXPECT_TRUE(statistics.status.empty());
    EXPECT_TRUE(statistics.technology.empty());
}

TEST(ProjectServiceTest, ConcurrentAddsAreSerializedByTheServiceMutex) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);

    constexpr int threadCount = 4;
    constexpr int projectsPerThread = 10;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int thread = 0; thread < threadCount; ++thread) {
        workers.emplace_back([&service, thread, projectsPerThread]() {
            for (int index = 0; index < projectsPerThread; ++index) {
                static_cast<void>(service.addProject(
                    "Project " + std::to_string(thread) + "-" + std::to_string(index),
                    {"C++"}, "", "active"));
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    const std::vector<devmanager::Project> projects = service.listProjects();
    ASSERT_EQ(projects.size(), static_cast<std::size_t>(threadCount * projectsPerThread));
    std::vector<devmanager::ProjectId> ids;
    ids.reserve(projects.size());
    for (const devmanager::Project& project : projects) {
        ids.push_back(project.id());
    }
    std::sort(ids.begin(), ids.end());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        EXPECT_EQ(ids[index], index + 1U);
    }
}

}  // namespace
