#include "application/ProjectManager.h"
#include "application/ProjectService.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <stdexcept>
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

TEST(ProjectServiceTest, PageProjectsUsesOneBasedPagesAndPreservesTotal) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    static_cast<void>(service.addProject("First", {"C++"}, "", "active"));
    static_cast<void>(service.addProject("Second", {"C++"}, "", "active"));

    devmanager::ProjectQuery query;
    query.status = "active";
    const devmanager::PagedProjects page = service.pageProjects(query, 2, 1);

    EXPECT_EQ(page.total, 2U);
    EXPECT_EQ(page.page, 2U);
    EXPECT_EQ(page.size, 1U);
    ASSERT_EQ(page.items.size(), 1U);
    EXPECT_EQ(page.items.front().name(), "Second");
}

TEST(ProjectServiceTest, PageProjectsRejectsInvalidPageSizeAndOverflow) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    const devmanager::ProjectQuery query;

    EXPECT_THROW(static_cast<void>(service.pageProjects(query, 0, 1)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.pageProjects(query, 1, 0)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.pageProjects(query, 1, 101)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(service.pageProjects(
                     query, std::numeric_limits<std::uint64_t>::max(), 2)),
                 std::invalid_argument);
}

TEST(ProjectServiceTest, PageProjectsReturnsEmptyItemsPastEndWithTotal) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    static_cast<void>(service.addProject("Only", {"C++"}, "", "active"));

    const devmanager::PagedProjects page = service.pageProjects({}, 2, 1);
    EXPECT_EQ(page.total, 1U);
    EXPECT_TRUE(page.items.empty());
}

TEST(ProjectServiceTest, PageProjectsReturnsEmptyForHugeOffsetWithoutRepositoryQuery) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    static_cast<void>(service.addProject("Only", {"C++"}, "", "active"));

    const devmanager::PagedProjects page = service.pageProjects(
        {}, std::numeric_limits<std::uint64_t>::max(), 1);

    EXPECT_EQ(page.total, 1U);
    EXPECT_EQ(page.page, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(page.size, 1U);
    EXPECT_TRUE(page.items.empty());
}

TEST(ProjectServiceTest, QueryProjectsRejectsCallerPaginationAndReturnsAllMatches) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    static_cast<void>(service.addProject("Active One", {"C++"}, "", "active"));
    static_cast<void>(service.addProject("Active Two", {"Rust"}, "", "active"));
    static_cast<void>(service.addProject("Done", {"C++"}, "", "done"));

    devmanager::ProjectQuery query;
    query.status = "active";
    const std::vector<devmanager::Project> matches = service.queryProjects(query);
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].name(), "Active One");
    EXPECT_EQ(matches[1].name(), "Active Two");

    query.offset = 1;
    EXPECT_THROW(static_cast<void>(service.queryProjects(query)), std::invalid_argument);
    query.offset = 0;
    query.limit = 1;
    EXPECT_THROW(static_cast<void>(service.queryProjects(query)), std::invalid_argument);
}

TEST(ProjectServiceTest, QueryProjectsPreservesFiltersAndSortSemantics) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    static_cast<void>(service.addProject("Zulu Tool", {"C++", "CMake"}, "", "active"));
    static_cast<void>(service.addProject("Alpha Tool", {"C++"}, "", "active"));
    static_cast<void>(service.addProject("Alpha Done", {"C++"}, "", "done"));

    devmanager::ProjectQuery query;
    query.name = "tool";
    query.status = " ACTIVE ";
    query.technology = "c++";
    query.sort = devmanager::ProjectSortKey::Name;
    const std::vector<devmanager::Project> matches = service.queryProjects(query);

    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].name(), "Alpha Tool");
    EXPECT_EQ(matches[1].name(), "Zulu Tool");
}

}  // namespace
