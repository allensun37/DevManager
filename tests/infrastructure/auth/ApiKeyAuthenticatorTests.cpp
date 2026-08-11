#include "infrastructure/auth/ApiKeyAuthenticator.h"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

TEST(ApiKeyAuthenticatorTests, AcceptsExactBearerHeaderForConfiguredKey) {
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    EXPECT_TRUE(authenticator.authenticate("Bearer test-key"));
}

TEST(ApiKeyAuthenticatorTests, RejectsMissingHeaderAndMalformedBearerForms) {
    const devmanager::ApiKeyAuthenticator authenticator("test-key");

    EXPECT_FALSE(authenticator.authenticate(""));
    EXPECT_FALSE(authenticator.authenticate("Basic test-key"));
    EXPECT_FALSE(authenticator.authenticate("bearer test-key"));
    EXPECT_FALSE(authenticator.authenticate("Bearer"));
    EXPECT_FALSE(authenticator.authenticate("Bearer "));
    EXPECT_FALSE(authenticator.authenticate("Bearer  test-key"));
    EXPECT_FALSE(authenticator.authenticate(" Bearer test-key"));
    EXPECT_FALSE(authenticator.authenticate("Bearer test-key "));
}

TEST(ApiKeyAuthenticatorTests, RejectsWrongKey) {
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    EXPECT_FALSE(authenticator.authenticate("Bearer wrong-key"));
}

TEST(ApiKeyAuthenticatorTests, RejectsEmptyConfiguredKey) {
    EXPECT_THROW((void)devmanager::ApiKeyAuthenticator(""), std::invalid_argument);
}
