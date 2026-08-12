#include "infrastructure/auth/EnvironmentApiKeyProvider.h"

#include <array>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::optional<std::string> readApiKey() {
#if defined(_WIN32)
    std::array<char, 32768> buffer{};
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableA(
        "DEVMANAGER_API_KEY", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        return std::string{};
    }
    if (length < buffer.size()) {
        return std::string(buffer.data(), length);
    }

    std::string value(length, '\0');
    const DWORD actualLength = GetEnvironmentVariableA(
        "DEVMANAGER_API_KEY", value.data(), static_cast<DWORD>(value.size() + 1));
    if (actualLength == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
        return std::nullopt;
    }
    value.resize(actualLength);
    return value;
#else
    if (const char* value = std::getenv("DEVMANAGER_API_KEY"); value != nullptr) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(const char* name) : name_(name) {
        if (const std::optional<std::string> value = readApiKey(); value.has_value()) {
            previousPresent_ = true;
            previous_ = *value;
        }
    }

    ~EnvironmentGuard() {
        bool restored = false;
#if defined(_WIN32)
        if (previousPresent_) {
            const int crtResult = _putenv_s(name_, previous_.c_str());
            const BOOL winResult = SetEnvironmentVariableA(name_, previous_.c_str());
            restored = crtResult == 0 && winResult != 0;
        } else {
            const int crtResult = _putenv_s(name_, "");
            const BOOL winResult = SetEnvironmentVariableA(name_, nullptr);
            restored = crtResult == 0 && winResult != 0;
        }
#else
        if (previousPresent_) {
            restored = setenv(name_, previous_.c_str(), 1) == 0;
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
    bool previousPresent_ = false;
    std::string previous_;
};

bool unsetApiKey() {
#if defined(_WIN32)
    return _putenv_s("DEVMANAGER_API_KEY", "") == 0 &&
           SetEnvironmentVariableA("DEVMANAGER_API_KEY", nullptr) != 0;
#else
    return unsetenv("DEVMANAGER_API_KEY") == 0;
#endif
}

bool setApiKey(const std::string& value) {
#if defined(_WIN32)
    return _putenv_s("DEVMANAGER_API_KEY", value.c_str()) == 0 &&
           SetEnvironmentVariableA("DEVMANAGER_API_KEY", value.c_str()) != 0;
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

TEST(EnvironmentApiKeyProviderTests, WhitespaceOnlyEnvironmentVariableIsRejected) {
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    ASSERT_TRUE(setApiKey(" \t\n"));

    const devmanager::EnvironmentApiKeyProvider provider;
    EXPECT_THROW((void)provider.load(), std::runtime_error);
}

TEST(EnvironmentApiKeyProviderTests, BoundaryWhitespaceEnvironmentVariableIsRejected) {
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    const devmanager::EnvironmentApiKeyProvider provider;

    ASSERT_TRUE(setApiKey(" test-key"));
    EXPECT_THROW((void)provider.load(), std::runtime_error);

    ASSERT_TRUE(setApiKey("test-key\t"));
    EXPECT_THROW((void)provider.load(), std::runtime_error);
}

TEST(EnvironmentApiKeyProviderTests, LoadsValueExactlyWithoutTrimmingOrPrintingIt) {
    EnvironmentGuard guard("DEVMANAGER_API_KEY");
    const std::string configured = "test-key\twith-internal-whitespace";
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

TEST(EnvironmentApiKeyProviderTests, RestoresExistingEmptyEnvironmentValueAfterScope) {
    EnvironmentGuard restoreOriginal("DEVMANAGER_API_KEY");
    ASSERT_TRUE(setApiKey(""));
    ASSERT_EQ(readApiKey(), std::optional<std::string>(""));

    {
        EnvironmentGuard guard("DEVMANAGER_API_KEY");
        ASSERT_TRUE(setApiKey("test-only-scope-value"));
        ASSERT_EQ(readApiKey(), std::optional<std::string>("test-only-scope-value"));
    }

    EXPECT_EQ(readApiKey(), std::optional<std::string>(""));
}
