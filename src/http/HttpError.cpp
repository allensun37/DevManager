#include "http/HttpError.h"

namespace devmanager {

nlohmann::json HttpError::toJson() const {
    return nlohmann::json{{"error", {{"code", code}, {"message", message}}}};
}

}  // namespace devmanager
