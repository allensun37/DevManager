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
    void saveStore(const ProjectStore& store) const override;

private:
    std::filesystem::path filePath_;
    std::shared_ptr<FileReplacer> fileReplacer_;
};

}  // namespace devmanager
