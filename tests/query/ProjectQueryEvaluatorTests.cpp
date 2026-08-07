#include "query/ProjectQueryEvaluator.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

devmanager::Project makeProject(devmanager::ProjectId id,
                                std::string name,
                                std::vector<std::string> techStack,
                                std::string status) {
    return {id, std::move(name), std::move(techStack), "Description", std::move(status)};
}

std::vector<devmanager::ProjectId> projectIds(const std::vector<devmanager::Project>& projects) {
    std::vector<devmanager::ProjectId> ids;
    ids.reserve(projects.size());
    for (const devmanager::Project& project : projects) {
        ids.push_back(project.id());
    }
    return ids;
}

TEST(ProjectQueryEvaluatorTest, SortsByIdByDefault) {
    const std::vector<devmanager::Project> projects{
        makeProject(3, "Third", {"C++"}, "Planned"),
        makeProject(1, "First", {"CMake"}, "Active"),
        makeProject(2, "Second", {"Linux"}, "Paused"),
    };

    const std::vector<devmanager::Project> result =
        devmanager::ProjectQueryEvaluator::query(projects, {});

    EXPECT_EQ(projectIds(result), (std::vector<devmanager::ProjectId>{1, 2, 3}));
}

TEST(ProjectQueryEvaluatorTest, SortsNamesWithIdTieBreaksBeforeSlicing) {
    const std::vector<devmanager::Project> projects{
        makeProject(3, "beta", {"C++"}, "Planned"),
        makeProject(1, "Alpha", {"CMake"}, "Active"),
        makeProject(2, "alpha", {"Linux"}, "Paused"),
    };
    devmanager::ProjectQuery query;
    query.sort = devmanager::ProjectSortKey::Name;
    query.offset = 1;
    query.limit = 1;

    const std::vector<devmanager::Project> result =
        devmanager::ProjectQueryEvaluator::query(projects, query);

    EXPECT_EQ(projectIds(result), (std::vector<devmanager::ProjectId>{2}));
}

TEST(ProjectQueryEvaluatorTest, SortsStatusesWithoutTrimmingAndUsesIdTieBreaks) {
    const std::vector<devmanager::Project> projects{
        makeProject(3, "Third", {"C++"}, "Planned"),
        makeProject(2, "Second", {"CMake"}, "Active"),
        makeProject(1, "First", {"Linux"}, "active"),
        makeProject(4, "Fourth", {"Rust"}, " active"),
    };
    devmanager::ProjectQuery query;
    query.sort = devmanager::ProjectSortKey::Status;

    const std::vector<devmanager::Project> result =
        devmanager::ProjectQueryEvaluator::query(projects, query);

    EXPECT_EQ(projectIds(result), (std::vector<devmanager::ProjectId>{4, 1, 2, 3}));
}

TEST(ProjectQueryEvaluatorTest, MatchesNamesByLowercaseSubstringWithoutTrimming) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "Alpha", {"C++"}, "Active"),
        makeProject(2, "Beta Alpha", {"CMake"}, "Paused"),
        makeProject(3, "Beta", {"Linux"}, "Planned"),
    };
    devmanager::ProjectQuery query;
    query.name = "ALP";
    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{1, 2}));

    query.name = " ALP";
    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{2}));

    query.name = "";
    EXPECT_TRUE(devmanager::ProjectQueryEvaluator::query(projects, query).empty());
}

TEST(ProjectQueryEvaluatorTest, MatchesStatusesByTrimmedLowercaseExactValue) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "First", {"C++"}, " In Progress "),
        makeProject(2, "Second", {"CMake"}, "in progress"),
        makeProject(3, "Third", {"Linux"}, "In progress soon"),
    };
    devmanager::ProjectQuery query;
    query.status = " \tIN PROGRESS\r\n";

    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{1, 2}));

    query.status = "";
    EXPECT_TRUE(devmanager::ProjectQueryEvaluator::query(projects, query).empty());
}

TEST(ProjectQueryEvaluatorTest, MatchesTechnologyWithCppPunctuationSubstringSemantics) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "First", {"C++ / CMake"}, "Active"),
        makeProject(2, "Second", {"C#"}, "Active"),
        makeProject(3, "Third", {"Rust"}, "Active"),
    };
    devmanager::ProjectQuery query;
    query.technology = "C++ / CMA";

    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{1}));

    query.technology = "";
    EXPECT_TRUE(devmanager::ProjectQueryEvaluator::query(projects, query).empty());
}

TEST(ProjectQueryEvaluatorTest, CombinesAllPresentFiltersWithAnd) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "Alpha", {"C++"}, "Active"),
        makeProject(2, "Alpha", {"Rust"}, "Active"),
        makeProject(3, "Alpha", {"C++"}, "Planned"),
        makeProject(4, "Beta", {"C++"}, "Active"),
    };
    devmanager::ProjectQuery query;
    query.name = "alp";
    query.status = " active ";
    query.technology = "cpp";

    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{1}));
}

TEST(ProjectQueryEvaluatorTest, TreatsZeroLimitAsUnpagedAndIgnoresOffset) {
    const std::vector<devmanager::Project> projects{
        makeProject(3, "Third", {"C++"}, "Planned"),
        makeProject(1, "First", {"CMake"}, "Active"),
        makeProject(2, "Second", {"Linux"}, "Paused"),
    };
    devmanager::ProjectQuery query;
    query.offset = 2;
    query.limit = 0;

    EXPECT_EQ(projectIds(devmanager::ProjectQueryEvaluator::query(projects, query)),
              (std::vector<devmanager::ProjectId>{1, 2, 3}));
}

TEST(ProjectQueryEvaluatorTest, ReturnsEmptyWhenPagedOffsetIsPastTheEnd) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "First", {"C++"}, "Active"),
        makeProject(2, "Second", {"CMake"}, "Paused"),
    };
    devmanager::ProjectQuery query;
    query.offset = 3;
    query.limit = 1;

    EXPECT_TRUE(devmanager::ProjectQueryEvaluator::query(projects, query).empty());
}

TEST(ProjectQueryEvaluatorTest, CountsFilteredProjectsWithoutOffsetOrLimit) {
    const std::vector<devmanager::Project> projects{
        makeProject(1, "Alpha", {"C++"}, "Active"),
        makeProject(2, "Alphabet", {"CMake"}, "Paused"),
        makeProject(3, "Beta", {"Linux"}, "Planned"),
    };
    devmanager::ProjectQuery query;
    query.name = "alpha";
    query.offset = 100;
    query.limit = 1;

    EXPECT_EQ(devmanager::ProjectQueryEvaluator::count(projects, query), 2U);
}

}  // namespace
