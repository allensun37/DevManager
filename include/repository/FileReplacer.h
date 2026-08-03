#pragma once

#include <filesystem>

namespace devmanager {

class FileReplacer {
public:
    virtual ~FileReplacer() = default;

    virtual void replace(const std::filesystem::path& temporaryFile,
                         const std::filesystem::path& targetFile) const = 0;
};

class PlatformFileReplacer final : public FileReplacer {
public:
    void replace(const std::filesystem::path& temporaryFile,
                 const std::filesystem::path& targetFile) const override;
};

}  // namespace devmanager
