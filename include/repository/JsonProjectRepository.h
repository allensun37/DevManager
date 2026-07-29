#pragma once

#include "repository/ProjectRepository.h"

#include <filesystem>

namespace devmanager {

class JsonProjectRepository final : public ProjectRepository {
public:
    explicit JsonProjectRepository(std::filesystem::path filePath);

    [[nodiscard]] ProjectStore loadStore() const override;
    void saveStore(const ProjectStore& store) const override;

private:
    std::filesystem::path filePath_;
};

}  // namespace devmanager
