#include "infrastructure/auth/EnvironmentApiKeyProvider.h"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(const char* name) : name_(name) {
        if (const char* value = std::getenv(name_); value != nullptr) {
            previous_ = std::string(value);
        }
    }

    ~EnvironmentGuard() {
        bool restored = false;
#if defined(_WIN32)
        if (previous_.has_value()) {
            restored = _putenv_s(name_, previous_->c_str()) == 0;
        } else {
            restored = _putenv_s(name_, "") == 0;
        }
#else
        if (previous_.has_value()) {
            restored = setenv(name_, previous_->c_str(), 1) == 0;
        } else {
            restored = unsetenv(name_) == 0;
        }
#endif
        if (!restored) {
            ADD_FAILURE() << "failed to restore DEVMANAGER_API_KEY";
        }
    }

    EnvironmentGuard(const EnvironmentGuard&) = delete;
    EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

std::optional<std::string> readApiKey() {
    if (const char* value = std::getenv("DEVMANAGER_API_KEY"); value != nullptr) {
        return std::string(value);
    }
    return std::nullopt;
}

bool unsetApiKey() {
#if defined(_WIN32)
    return _putenv_s("DEVMANAGER_API_KEY", "") == 0;
#else
    return unsetenv("DEVMANAGER_API_KEY") == 0;
#endif
}

bool setApiKey(const std::string& value) {
#if defined(_WIN32)
    return _putenv_s("DEVMANAGER_API_KEY", value.c_str()) == 0;
#else
    return setenv("DEVMANAGER_API_KEY", value.c_str(), 1) == 0;
#endif
}

} // namespace

TEST(EnvironmentApiKeyProviderTests, MissingEnvironmentVariableIsRejectedWithoutLeakingSecret) {
    const std::string sentinel = "test-only-sentinel-secret";
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    ASSERT_TRUE(setApiKey(sentinel));
    ASSERT_TRUE(unsetApiKey());

    const devmanager::EnvironmentApiKeyProvider provider;
    try {
        (void)provider.load();
        FAIL() << "missing API key should be rejected";
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("DEVMANAGER_API_KEY"), std::string::npos);
        EXPECT_EQ(message.find(sentinel), std::string::npos);
    }
}

TEST(EnvironmentApiKeyProviderTests, EmptyEnvironmentVariableIsRejected) {
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    ASSERT_TRUE(setApiKey(""));

    const devmanager::EnvironmentApiKeyProvider provider;
    EXPECT_THROW((void)provider.load(), std::runtime_error);
}

TEST(EnvironmentApiKeyProviderTests, LoadsValueExactlyWithoutTrimmingOrPrintingIt) {
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    const std::string configured = "  test-key-with-spaces  ";
    ASSERT_TRUE(setApiKey(configured));

    const devmanager::EnvironmentApiKeyProvider provider;
    EXPECT_EQ(provider.load(), configured);
}

TEST(EnvironmentApiKeyProviderTests, RestoresOriginalEnvironmentValueAfterScope) {
    const std::optional<std::string> original = readApiKey();
    {
        EnvironmentGuard guard("DEVMANAGER_API_KEY");
        ASSERT_TRUE(setApiKey("test-only-scope-value"));
        ASSERT_EQ(readApiKey(), std::optional<std::string>("test-only-scope-value"));
    }

    EXPECT_EQ(readApiKey(), original);
}
