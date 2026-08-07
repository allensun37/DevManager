#pragma once

#include "repository/ProjectRepository.h"

#include <memory>

namespace devmanager {

class SqliteConnection;

class SqliteProjectRepository final : public ProjectRepository {
public:
    explicit SqliteProjectRepository(std::unique_ptr<SqliteConnection> connection);
    ~SqliteProjectRepository() override;

    [[nodiscard]] ProjectStore loadStore() const override;
    void create(const Project& project, ProjectId nextIdAfterCreate) override;
    void update(const Project& project) override;
    void remove(ProjectId id) override;
    [[nodiscard]] std::optional<Project> findById(ProjectId id) const override;
    [[nodiscard]] std::vector<Project> query(const ProjectQuery& projectQuery) const override;
    [[nodiscard]] std::uint64_t count(const ProjectQuery& projectQuery) const override;

private:
    std::unique_ptr<SqliteConnection> connection_;
};

}  // namespace devmanager
