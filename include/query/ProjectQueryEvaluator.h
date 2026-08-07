#pragma once

#include "query/ProjectQuery.h"

#include <cstdint>
#include <vector>

namespace devmanager {

class ProjectQueryEvaluator {
public:
    [[nodiscard]] static std::vector<Project> query(const std::vector<Project>& projects,
                                                    const ProjectQuery& projectQuery);
    [[nodiscard]] static std::uint64_t count(const std::vector<Project>& projects,
                                             const ProjectQuery& projectQuery);
};

}  // namespace devmanager
