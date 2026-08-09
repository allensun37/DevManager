#include "repository/SqliteProjectRepository.h"

#include "common/ProjectSearchText.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"
#include "infrastructure/sqlite/SqliteTransaction.h"
#include "repository/ProjectStoreValidator.h"
#include "repository/SqliteProjectIdCodec.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace devmanager {
namespace {

struct StoredProject final {
    ProjectId id;
    std::string name;
    std::string description;
    std::string status;
    std::vector<std::string> tags;
};

class SqliteReadTransaction final {
public:
    explicit SqliteReadTransaction(SqliteConnection& connection)
        : connection_(connection) {
        connection_.execute("BEGIN DEFERRED");
        try {
            // Establish the read snapshot before callers prepare their first data query.
            connection_.execute("SELECT singleton FROM repository_state LIMIT 1");
        } catch (...) {
            try {
                connection_.execute("ROLLBACK");
            } catch (...) {
            }
            throw;
        }
    }

    ~SqliteReadTransaction() noexcept {
        if (!active_) {
            return;
        }
        try {
            connection_.execute("ROLLBACK");
        } catch (...) {
        }
    }

    SqliteReadTransaction(const SqliteReadTransaction&) = delete;
    SqliteReadTransaction& operator=(const SqliteReadTransaction&) = delete;

    void commit() {
        if (!active_) {
            throw std::runtime_error(
                "failed to commit SQLite read transaction: transaction is inactive");
        }
        connection_.execute("COMMIT");
        active_ = false;
    }

private:
    SqliteConnection& connection_;
    bool active_ {true};
};

[[noreturn]] void throwUnexpectedChanges(std::string_view operation) {
    throw std::runtime_error(std::string(operation) + ": expected exactly one changed row");
}

void requireSingleChange(const SqliteConnection& connection, std::string_view operation) {
    if (connection.changes() != 1) {
        throwUnexpectedChanges(operation);
    }
}

void validateStoreForWrite(const ProjectStore& store) {
    try {
        validateProjectStore(store);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("invalid SQLite project store: ") + error.what());
    }
}

void bindProjectColumns(SqliteStatement& statement, const Project& project) {
    statement.bindText(1, project.name());
    statement.bindText(2, project_search_text::normalizeName(project.name()));
    statement.bindText(3, project.description());
    statement.bindText(4, project.status());
    statement.bindText(5, project_search_text::normalizeStatus(project.status()));
    statement.bindText(6, project_search_text::statusSortKey(project.status()));
}

void insertTags(SqliteConnection& connection, const Project& project) {
    const auto& tags = project.techStack();
    if (tags.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("cannot persist project tags: too many tags");
    }

    const std::string encodedId = SqliteProjectIdCodec::encode(project.id());
    for (std::size_t position = 0; position < tags.size(); ++position) {
        auto insert = connection.prepare(
            "INSERT INTO project_tags(project_id, position, tag, normalized_tag) "
            "VALUES (?1, ?2, ?3, ?4)");
        insert.bindText(1, encodedId);
        insert.bindInt64(2, static_cast<std::int64_t>(position));
        insert.bindText(3, tags[position]);
        insert.bindText(4, project_search_text::normalizeTechnology(tags[position]));
        insert.executeDone();
        requireSingleChange(connection, "failed to insert SQLite project tag");
    }
}

std::int64_t readTagPosition(const SqliteStatement& statement,
                             int positionColumn,
                             int storageClassColumn) {
    if (statement.columnText(storageClassColumn) != "integer") {
        throw std::runtime_error("invalid SQLite project tag position storage class");
    }
    return statement.columnInt64(positionColumn);
}

std::vector<std::string> loadTags(SqliteConnection& connection, ProjectId id) {
    auto statement = connection.prepare(
        "SELECT position,tag,typeof(position) FROM project_tags "
        "WHERE project_id = ?1 ORDER BY position ASC");
    statement.bindText(1, SqliteProjectIdCodec::encode(id));

    std::vector<std::string> tags;
    while (statement.stepRow()) {
        const std::int64_t position = readTagPosition(statement, 0, 2);
        if (position < 0 || static_cast<std::uint64_t>(position) != tags.size()) {
            throw std::runtime_error("invalid SQLite project tag positions");
        }
        tags.push_back(statement.columnText(1));
    }
    return tags;
}

Project hydrateProject(SqliteConnection& connection,
                       ProjectId id,
                       std::string name,
                       std::string description,
                       std::string status) {
    return Project{id,
                   std::move(name),
                   loadTags(connection, id),
                   std::move(description),
                   std::move(status)};
}

ProjectStore loadStoreImpl(SqliteConnection& connection) {
    try {
        ProjectStore store;
        {
            auto state = connection.prepare(
                "SELECT singleton,next_id FROM repository_state");
            if (!state.stepRow()) {
                throw std::runtime_error("repository_state singleton is missing");
            }
            if (state.columnInt64(0) != 1) {
                throw std::runtime_error("repository_state singleton is invalid");
            }
            store.nextId = SqliteProjectIdCodec::decode(state.columnText(1));
            if (state.stepRow()) {
                throw std::runtime_error("repository_state must contain exactly one row");
            }
        }

        std::vector<StoredProject> storedProjects;
        std::unordered_map<ProjectId, std::size_t> projectIndexes;
        {
            auto projects = connection.prepare(
                "SELECT id,name,description,status FROM projects ORDER BY id ASC");
            while (projects.stepRow()) {
                const ProjectId id = SqliteProjectIdCodec::decode(projects.columnText(0));
                const std::size_t index = storedProjects.size();
                if (!projectIndexes.emplace(id, index).second) {
                    throw std::runtime_error("SQLite project IDs must be unique");
                }
                storedProjects.push_back(StoredProject{
                    id,
                    projects.columnText(1),
                    projects.columnText(2),
                    projects.columnText(3),
                    {},
                });
            }
        }

        {
            auto tags = connection.prepare(
                "SELECT project_id,position,tag,typeof(position) FROM project_tags "
                "ORDER BY project_id ASC,position ASC");
            while (tags.stepRow()) {
                const ProjectId projectId = SqliteProjectIdCodec::decode(tags.columnText(0));
                const auto projectIndex = projectIndexes.find(projectId);
                if (projectIndex == projectIndexes.end()) {
                    throw std::runtime_error("SQLite project tag references a missing project");
                }

                StoredProject& project = storedProjects[projectIndex->second];
                const std::int64_t position = readTagPosition(tags, 1, 3);
                if (position < 0 ||
                    static_cast<std::uint64_t>(position) != project.tags.size()) {
                    throw std::runtime_error("invalid SQLite project tag positions");
                }
                project.tags.push_back(tags.columnText(2));
            }
        }

        store.projects.reserve(storedProjects.size());
        for (StoredProject& project : storedProjects) {
            store.projects.emplace_back(project.id,
                                        std::move(project.name),
                                        std::move(project.tags),
                                        std::move(project.description),
                                        std::move(project.status));
        }

        validateProjectStore(store);
        return store;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("invalid SQLite project store: ") + error.what());
    }
}

struct QueryFilter final {
    std::string where;
    std::vector<std::string> bindings;
};

QueryFilter buildQueryFilter(const ProjectQuery& projectQuery) {
    QueryFilter filter;
    if (projectQuery.name.has_value()) {
        const std::string normalized =
            project_search_text::normalizeName(*projectQuery.name);
        if (normalized.empty()) {
            filter.where = "0";
        } else {
            filter.where = "instr(p.normalized_name, ?) > 0";
            filter.bindings.push_back(normalized);
        }
    }
    if (projectQuery.status.has_value()) {
        const std::string normalized =
            project_search_text::normalizeStatus(*projectQuery.status);
        if (normalized.empty()) {
            filter.where = filter.where.empty() ? "0" : filter.where + " AND 0";
        } else {
            const std::string condition = "p.normalized_status = ?";
            filter.where = filter.where.empty() ? condition : filter.where + " AND " + condition;
            filter.bindings.push_back(normalized);
        }
    }
    if (projectQuery.technology.has_value()) {
        const std::string normalized =
            project_search_text::normalizeTechnology(*projectQuery.technology);
        if (normalized.empty()) {
            filter.where = filter.where.empty() ? "0" : filter.where + " AND 0";
        } else {
            const std::string condition =
                "EXISTS (SELECT 1 FROM project_tags filter_tag WHERE "
                "filter_tag.project_id = p.id AND instr(filter_tag.normalized_tag, ?) > 0)";
            filter.where = filter.where.empty() ? condition : filter.where + " AND " + condition;
            filter.bindings.push_back(normalized);
        }
    }
    return filter;
}

std::string sortSql(ProjectSortKey sort) {
    switch (sort) {
    case ProjectSortKey::Id:
        return "p.id ASC";
    case ProjectSortKey::Name:
        return "p.normalized_name ASC, p.id ASC";
    case ProjectSortKey::Status:
        return "p.status_sort_key ASC, p.id ASC";
    }
    throw std::runtime_error("failed to query SQLite projects: unsupported project sort key");
}

void bindFilter(SqliteStatement& statement, const QueryFilter& filter) {
    int index = 1;
    for (const std::string& binding : filter.bindings) {
        statement.bindText(index++, binding);
    }
}

std::vector<Project> readQueriedProjects(SqliteConnection& connection,
                                         const ProjectQuery& projectQuery,
                                         const QueryFilter& filter) {
    std::string sql =
        "SELECT p.id,p.name,p.description,p.status FROM projects p";
    if (!filter.where.empty()) {
        sql += " WHERE " + filter.where;
    }
    sql += " ORDER BY " + sortSql(projectQuery.sort);

    const bool paged = projectQuery.limit != 0;
    if (paged) {
        constexpr std::uint64_t maxInt64 =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (projectQuery.offset > maxInt64 || projectQuery.limit > maxInt64) {
            throw std::runtime_error(
                "failed to query SQLite projects: offset/limit exceeds SQLite integer range");
        }
        sql += " LIMIT ? OFFSET ?";
    }

    auto projects = connection.prepare(sql);
    bindFilter(projects, filter);
    int nextBinding = static_cast<int>(filter.bindings.size()) + 1;
    if (paged) {
        projects.bindInt64(nextBinding++, static_cast<std::int64_t>(projectQuery.limit));
        projects.bindInt64(nextBinding, static_cast<std::int64_t>(projectQuery.offset));
    }

    std::vector<StoredProject> storedProjects;
    std::unordered_map<ProjectId, std::size_t> projectIndexes;
    while (projects.stepRow()) {
        const ProjectId id = SqliteProjectIdCodec::decode(projects.columnText(0));
        const std::size_t index = storedProjects.size();
        if (!projectIndexes.emplace(id, index).second) {
            throw std::runtime_error("SQLite project IDs must be unique");
        }
        storedProjects.push_back(StoredProject{
            id,
            projects.columnText(1),
            projects.columnText(2),
            projects.columnText(3),
            {},
        });
    }

    if (storedProjects.empty()) {
        return {};
    }

    std::string tagsSql =
        "SELECT project_id,position,tag,typeof(position) FROM project_tags WHERE project_id IN (";
    for (std::size_t index = 0; index < storedProjects.size(); ++index) {
        if (index != 0) {
            tagsSql += ',';
        }
        tagsSql += '?';
        tagsSql += std::to_string(index + 1);
    }
    tagsSql += ") ORDER BY project_id ASC,position ASC";

    auto tags = connection.prepare(tagsSql);
    for (std::size_t index = 0; index < storedProjects.size(); ++index) {
        tags.bindText(static_cast<int>(index + 1),
                      SqliteProjectIdCodec::encode(storedProjects[index].id));
    }
    while (tags.stepRow()) {
        const ProjectId projectId = SqliteProjectIdCodec::decode(tags.columnText(0));
        const auto projectIndex = projectIndexes.find(projectId);
        if (projectIndex == projectIndexes.end()) {
            throw std::runtime_error("SQLite project tag references a missing project");
        }
        StoredProject& project = storedProjects[projectIndex->second];
        const std::int64_t position = readTagPosition(tags, 1, 3);
        if (position < 0 || static_cast<std::uint64_t>(position) != project.tags.size()) {
            throw std::runtime_error("invalid SQLite project tag positions");
        }
        project.tags.push_back(tags.columnText(2));
    }

    std::vector<Project> result;
    result.reserve(storedProjects.size());
    for (StoredProject& project : storedProjects) {
        result.emplace_back(project.id,
                            std::move(project.name),
                            std::move(project.tags),
                            std::move(project.description),
                            std::move(project.status));
    }
    return result;
}

}  // namespace

SqliteProjectRepository::SqliteProjectRepository(
    std::unique_ptr<SqliteConnection> connection)
    : connection_(std::move(connection)) {
    if (connection_ == nullptr) {
        throw std::invalid_argument("SQLite connection must not be null");
    }
}

SqliteProjectRepository::~SqliteProjectRepository() = default;

ProjectStore SqliteProjectRepository::loadStore() const {
    SqliteReadTransaction transaction(*connection_);
    ProjectStore store = loadStoreImpl(*connection_);
    transaction.commit();
    return store;
}

void SqliteProjectRepository::create(const Project& project,
                                     ProjectId nextIdAfterCreate) {
    SqliteTransaction transaction(*connection_);

    ProjectStore candidate = loadStoreImpl(*connection_);
    candidate.projects.push_back(project);
    candidate.nextId = nextIdAfterCreate;
    validateStoreForWrite(candidate);

    auto insert = connection_->prepare(
        "INSERT INTO projects("
        "id,name,normalized_name,description,status,normalized_status,status_sort_key) "
        "VALUES (?1,?2,?3,?4,?5,?6,?7)");
    insert.bindText(1, SqliteProjectIdCodec::encode(project.id()));
    insert.bindText(2, project.name());
    insert.bindText(3, project_search_text::normalizeName(project.name()));
    insert.bindText(4, project.description());
    insert.bindText(5, project.status());
    insert.bindText(6, project_search_text::normalizeStatus(project.status()));
    insert.bindText(7, project_search_text::statusSortKey(project.status()));
    insert.executeDone();
    requireSingleChange(*connection_, "failed to insert SQLite project");

    insertTags(*connection_, project);

    auto updateState = connection_->prepare(
        "UPDATE repository_state SET next_id = ?1 WHERE singleton = 1");
    updateState.bindText(1, SqliteProjectIdCodec::encode(nextIdAfterCreate));
    updateState.executeDone();
    requireSingleChange(*connection_, "failed to update SQLite repository state");

    transaction.commit();
}

void SqliteProjectRepository::update(const Project& project) {
    SqliteTransaction transaction(*connection_);

    auto updateProject = connection_->prepare(
        "UPDATE projects SET "
        "name=?1,normalized_name=?2,description=?3,status=?4,normalized_status=?5,"
        "status_sort_key=?6 WHERE id=?7");
    bindProjectColumns(updateProject, project);
    updateProject.bindText(7, SqliteProjectIdCodec::encode(project.id()));
    updateProject.executeDone();
    requireSingleChange(*connection_, "failed to update SQLite project");

    auto deleteTags = connection_->prepare(
        "DELETE FROM project_tags WHERE project_id = ?1");
    deleteTags.bindText(1, SqliteProjectIdCodec::encode(project.id()));
    deleteTags.executeDone();

    insertTags(*connection_, project);
    transaction.commit();
}

void SqliteProjectRepository::remove(ProjectId id) {
    SqliteTransaction transaction(*connection_);

    auto removeProject = connection_->prepare("DELETE FROM projects WHERE id = ?1");
    removeProject.bindText(1, SqliteProjectIdCodec::encode(id));
    removeProject.executeDone();
    requireSingleChange(*connection_, "failed to remove SQLite project");

    transaction.commit();
}

std::optional<Project> SqliteProjectRepository::findById(ProjectId id) const {
    try {
        SqliteReadTransaction transaction(*connection_);
        auto project = connection_->prepare(
            "SELECT id,name,description,status FROM projects WHERE id = ?1");
        project.bindText(1, SqliteProjectIdCodec::encode(id));
        if (!project.stepRow()) {
            transaction.commit();
            return std::nullopt;
        }

        const ProjectId storedId = SqliteProjectIdCodec::decode(project.columnText(0));
        Project result = hydrateProject(*connection_,
                                        storedId,
                                        project.columnText(1),
                                        project.columnText(2),
                                        project.columnText(3));
        if (project.stepRow()) {
            throw std::runtime_error("SQLite project ID is not unique");
        }
        transaction.commit();
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("failed to find SQLite project: ") + error.what());
    }
}

std::vector<Project> SqliteProjectRepository::query(const ProjectQuery& projectQuery) const {
    try {
        SqliteReadTransaction transaction(*connection_);
        static_cast<void>(sortSql(projectQuery.sort));
        const QueryFilter filter = buildQueryFilter(projectQuery);
        std::vector<Project> result =
            readQueriedProjects(*connection_, projectQuery, filter);
        transaction.commit();
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("failed to query SQLite projects: ") + error.what());
    }
}

std::uint64_t SqliteProjectRepository::count(const ProjectQuery& projectQuery) const {
    try {
        SqliteReadTransaction transaction(*connection_);
        static_cast<void>(sortSql(projectQuery.sort));
        const QueryFilter filter = buildQueryFilter(projectQuery);
        std::string sql = "SELECT COUNT(*) FROM projects p";
        if (!filter.where.empty()) {
            sql += " WHERE " + filter.where;
        }
        auto statement = connection_->prepare(sql);
        bindFilter(statement, filter);
        if (!statement.stepRow()) {
            throw std::runtime_error("SQLite count query returned no row");
        }
        const std::int64_t count = statement.columnInt64(0);
        if (count < 0) {
            throw std::runtime_error("SQLite count query returned a negative value");
        }
        if (statement.stepRow()) {
            throw std::runtime_error("SQLite count query returned multiple rows");
        }
        transaction.commit();
        return static_cast<std::uint64_t>(count);
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("failed to count SQLite projects: ") + error.what());
    }
}

}  // namespace devmanager
