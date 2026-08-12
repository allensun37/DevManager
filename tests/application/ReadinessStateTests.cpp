#include "application/ReadinessState.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

TEST(ReadinessStateTest, StartsNotReadyAndMarksReady) {
    devmanager::ReadinessState state;

    EXPECT_EQ(state.state(), devmanager::ServiceState::Starting);
    EXPECT_FALSE(state.isReady());

    state.markReady();

    EXPECT_EQ(state.state(), devmanager::ServiceState::Ready);
    EXPECT_TRUE(state.isReady());
}

TEST(ReadinessStateTest, StoppingAndFailureAreNotReady) {
    devmanager::ReadinessState state;

    state.markReady();
    state.markStopping();
    EXPECT_EQ(state.state(), devmanager::ServiceState::Stopping);
    EXPECT_FALSE(state.isReady());

    state.markFailed();
    EXPECT_EQ(state.state(), devmanager::ServiceState::Failed);
    EXPECT_FALSE(state.isReady());
}

TEST(ReadinessStateTest, StoppedStateIsTerminal) {
    devmanager::ReadinessState state;

    state.markReady();
    state.markStopping();
    state.markStopped();
    state.markReady();
    state.markFailed();

    EXPECT_EQ(state.state(), devmanager::ServiceState::Stopped);
    EXPECT_FALSE(state.isReady());
}

TEST(ReadinessStateTest, FailedStateIsTerminal) {
    devmanager::ReadinessState state;

    state.markFailed();
    state.markReady();
    state.markStopping();
    state.markStopped();

    EXPECT_EQ(state.state(), devmanager::ServiceState::Failed);
    EXPECT_FALSE(state.isReady());
}

TEST(ReadinessStateTest, SupportsConcurrentReadersWhileStateChanges) {
    devmanager::ReadinessState state;
    std::atomic<bool> keepReading {true};
    std::atomic<unsigned int> observedReads {0U};
    std::mutex startupMutex;
    std::condition_variable startupCondition;
    unsigned int readersReady {0U};

    const auto reader = [&state,
                         &keepReading,
                         &observedReads,
                         &startupMutex,
                         &startupCondition,
                         &readersReady]() {
        {
            const std::lock_guard<std::mutex> lock(startupMutex);
            ++readersReady;
        }
        startupCondition.notify_one();
        while (keepReading.load()) {
            static_cast<void>(state.state());
            static_cast<void>(state.isReady());
            observedReads.fetch_add(1U);
        }
    };

    std::thread firstReader(reader);
    std::thread secondReader(reader);

    {
        std::unique_lock<std::mutex> lock(startupMutex);
        const bool started = startupCondition.wait_for(
            lock, std::chrono::seconds(5), [&readersReady]() { return readersReady == 2U; });
        if (!started) {
            keepReading.store(false);
            lock.unlock();
            firstReader.join();
            secondReader.join();
            FAIL() << "readers did not start";
        }
    }

    for (unsigned int iteration = 0U; iteration < 10000U; ++iteration) {
        state.markReady();
        state.markStopping();
    }
    state.markStopped();
    keepReading.store(false);

    firstReader.join();
    secondReader.join();

    EXPECT_GT(observedReads.load(), 0U);
    EXPECT_EQ(state.state(), devmanager::ServiceState::Stopped);
    EXPECT_FALSE(state.isReady());
}

}  // namespace
