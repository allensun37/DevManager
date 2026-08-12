#include "infrastructure/runtime/SignalHandler.h"

#include <gtest/gtest.h>

namespace {

TEST(SignalHandlerTest, StartsWithoutStopRequests) {
    devmanager::SignalHandler handler;

    EXPECT_FALSE(handler.stopRequested());
    EXPECT_FALSE(handler.forceStopRequested());
}

TEST(SignalHandlerTest, FirstRequestSetsStopOnly) {
    devmanager::SignalHandler handler;

    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_FALSE(handler.forceStopRequested());
}

TEST(SignalHandlerTest, SecondRequestSetsForceStop) {
    devmanager::SignalHandler handler;

    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_TRUE(handler.forceStopRequested());
}

TEST(SignalHandlerTest, FurtherRequestsKeepForceStopSet) {
    devmanager::SignalHandler handler;

    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();
    handler.recordStopRequestForTest();

    EXPECT_TRUE(handler.stopRequested());
    EXPECT_TRUE(handler.forceStopRequested());
}

}  // namespace
