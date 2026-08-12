#include "infrastructure/runtime/SignalHandler.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {

struct SignalHandlerPlatformCounters {
    unsigned int installCalls {0U};
    unsigned int uninstallCalls {0U};
};

class FakeSignalHandlerPlatform final : public devmanager::SignalHandlerPlatform {
public:
    explicit FakeSignalHandlerPlatform(
        std::shared_ptr<SignalHandlerPlatformCounters> counters =
            std::make_shared<SignalHandlerPlatformCounters>())
        : counters_(std::move(counters)) {}

    void install() override {
        ++counters_->installCalls;
        if (failInstall) {
            throw std::runtime_error("injected signal installation failure");
        }
    }

    void uninstall() noexcept override {
        ++counters_->uninstallCalls;
    }

    bool failInstall {false};

private:
    std::shared_ptr<SignalHandlerPlatformCounters> counters_;
};

class SignalHandlerTest : public testing::Test {
protected:
    void SetUp() override {
        handler.resetStopRequestsForTest();
    }

    devmanager::SignalHandler handler {
        std::make_unique<FakeSignalHandlerPlatform>()};
};

TEST_F(SignalHandlerTest, StartsWithoutStopRequests) {

    EXPECT_FALSE(handler.stopRequested());
    EXPECT_FALSE(handler.forceStopRequested());
}

TEST_F(SignalHandlerTest, FirstRequestSetsStopOnly) {

    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_FALSE(handler.forceStopRequested());
}

TEST_F(SignalHandlerTest, SecondRequestSetsForceStop) {

    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_TRUE(handler.forceStopRequested());
}

TEST_F(SignalHandlerTest, FurtherRequestsKeepForceStopSet) {

    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_TRUE(handler.forceStopRequested());
}

TEST(SignalHandlerInstallationTest, InstallsOnlyOnceAndUninstallsOnDestruction) {
    const auto counters = std::make_shared<SignalHandlerPlatformCounters>();
    auto platform = std::make_unique<FakeSignalHandlerPlatform>(counters);

    {
        devmanager::SignalHandler handler(std::move(platform));
        handler.install();
        handler.install();

        EXPECT_EQ(counters->installCalls, 1U);
        EXPECT_EQ(counters->uninstallCalls, 0U);
    }

    EXPECT_EQ(counters->uninstallCalls, 1U);
}

TEST(SignalHandlerInstallationTest, FailedInstallReleasesTheSingleHandlerReservation) {
    auto failingPlatform = std::make_unique<FakeSignalHandlerPlatform>();
    failingPlatform->failInstall = true;
    devmanager::SignalHandler failingHandler(std::move(failingPlatform));

    EXPECT_THROW(failingHandler.install(), std::runtime_error);

    const auto succeedingCounters = std::make_shared<SignalHandlerPlatformCounters>();
    auto succeedingPlatform = std::make_unique<FakeSignalHandlerPlatform>(succeedingCounters);
    devmanager::SignalHandler succeedingHandler(std::move(succeedingPlatform));
    EXPECT_NO_THROW(succeedingHandler.install());
    EXPECT_EQ(succeedingCounters->installCalls, 1U);
}

}  // namespace
