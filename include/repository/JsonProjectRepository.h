#pragma once

#include "repository/ProjectRepository.h"

#include <filesystem>
#include <memory>

namespace devmanager {

class FileReplacer;

class JsonProjectRepository final : public ProjectRepository {
public:
    explicit JsonProjectRepository(std::filesystem::path filePath);
    JsonProjectRepository(std::filesystem::path filePath,
                          std::shared_ptr<FileReplacer> fileReplacer);

    [[nodiscard]] ProjectStore loadStore() const override;
    void create(const Project& project, ProjectId nextIdAfterCreate) override;
    void update(const Project& project) override;
    void remove(ProjectId id) override;
    [[nodiscard]] std::optional<Project> findById(ProjectId id) const override;
    [[nodiscard]] std::vector<Project> query(const ProjectQuery& projectQuery) const override;
    [[nodiscard]] std::uint64_t count(const ProjectQuery& projectQuery) const override;

    void saveStore(const ProjectStore& store) const;

private:
    std::filesystem::path filePath_;
    std::shared_ptr<FileReplacer> fileReplacer_;
};

}  // namespace devmanager
