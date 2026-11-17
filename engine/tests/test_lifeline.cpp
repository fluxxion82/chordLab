#include "doctest.h"

#include "chordlab/lifeline.h"
#include "chordlab/link.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace chordlab;

namespace {

using Hook = LinkRegistry::Hook;
using Hooks = LinkRegistry::Hooks;

/// A flag one thread raises and another waits for: the tests' only synchronisation (no sleeps).
class Signal {
public:
    void raise() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            raised_ = true;
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return raised_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool raised_ = false;
};

/// What a republish writes to: the instance a hook would reach.
struct Target {
    int republished = 0;
};

/// Joins on every exit, a throwing REQUIRE included (a joinable std::thread destroyed would terminate).
struct Joiner {
    std::thread& thread;
    ~Joiner() {
        if (thread.joinable()) thread.join();
    }
};

}  // namespace

TEST_CASE("lifeline: a guarded hook calls through while the lifeline is alive") {
    auto lifeline = std::make_shared<Lifeline>();
    Target target;
    const Hook hook = guardedHook(lifeline, [&target] { ++target.republished; });
    REQUIRE(static_cast<bool>(hook));
    hook();
    hook();
    CHECK(target.republished == 2);
    CHECK(lifeline->alive);
}

TEST_CASE("lifeline: after retire a guarded hook does nothing, however often it is called") {
    auto lifeline = std::make_shared<Lifeline>();
    Target target;
    const Hook hook = guardedHook(lifeline, [&target] { ++target.republished; });
    hook();
    retire(*lifeline);
    CHECK_FALSE(lifeline->alive);
    hook();
    hook();
    CHECK(target.republished == 1);
    retire(*lifeline);  // idempotent
    hook();
    CHECK(target.republished == 1);
}

TEST_CASE("lifeline: a null lifeline or an empty republish gives an empty hook") {
    CHECK_FALSE(static_cast<bool>(guardedHook(nullptr, [] {})));
    CHECK_FALSE(static_cast<bool>(guardedHook(std::make_shared<Lifeline>(), std::function<void()>())));
}

TEST_CASE("lifeline: the hook holds the lifeline, not the target") {
    auto lifeline = std::make_shared<Lifeline>();
    const std::weak_ptr<Lifeline> weak = lifeline;
    Hook hook = guardedHook(lifeline, [] {});
    lifeline.reset();
    CHECK_FALSE(weak.expired());  // the hook keeps it
    hook = nullptr;
    CHECK(weak.expired());        // and nothing else does
}

TEST_CASE("lifeline: retire waits for a guarded call in progress on another thread") {
    auto lifeline = std::make_shared<Lifeline>();
    Signal entered;  // the hook is inside republish (and so holds the lifeline's mutex)
    Signal release;  // the test lets republish return
    Signal retiring; // the retiring thread is about to call retire
    std::atomic<bool> left{false};
    std::atomic<bool> leftWhenRetired{false};
    std::atomic<bool> retired{false};
    bool retiredWhileInside = true;

    const Hook hook = guardedHook(lifeline, [&] {
        entered.raise();
        release.wait();
        retiredWhileInside = retired.load();  // retire cannot have returned while this runs
        left = true;
    });

    std::thread caller([&hook] { hook(); });
    Joiner joinCaller{caller};
    entered.wait();

    std::thread retirer([&] {
        retiring.raise();
        retire(*lifeline);
        leftWhenRetired = left.load();
        retired = true;
    });
    Joiner joinRetirer{retirer};
    retiring.wait();
    CHECK_FALSE(retired.load());  // republish has not been released yet, so retire cannot have returned
    release.raise();

    caller.join();
    retirer.join();
    CHECK(left.load());
    CHECK(leftWhenRetired.load());   // retire returned only after republish left
    CHECK_FALSE(retiredWhileInside);
    CHECK_FALSE(lifeline->alive);
}

TEST_CASE("lifeline: a hook copied into a Hooks list outlives its target and touches nothing of it") {
    auto lifeline = std::make_shared<Lifeline>();
    auto target = std::make_unique<Target>();
    Target* raw = target.get();
    Hooks hooks;
    hooks.push_back(std::make_shared<const Hook>(guardedHook(lifeline, [raw] { ++raw->republished; })));
    hooks.push_back(hooks.front());  // the same hook returned by two operations

    (*hooks.front())();
    CHECK(target->republished == 1);

    // The instance's destructor: retire, then everything goes (the target, the instance's own lifeline pointer).
    retire(*lifeline);
    target.reset();
    lifeline.reset();

    // Another instance runs what an operation returned it, after the target is gone: nothing is touched (ASan
    // reports a use after free if the hook reached `raw`).
    for (const auto& hook : hooks) {
        REQUIRE(hook != nullptr);
        REQUIRE(static_cast<bool>(*hook));
        (*hook)();
    }
}

TEST_CASE("lifeline: a hook called in a loop on one thread while another retires and destroys the target") {
    // Under TSan: republish writes the target without atomics, and the retiring thread deletes it right after
    // retire. Only the lifeline's mutex orders the two; a hook that skipped the mutex or the `alive` check would be
    // a data race and a use after free.
    for (int round = 0; round < 50; ++round) {
        auto lifeline = std::make_shared<Lifeline>();
        auto* target = new Target;
        const Hook hook = guardedHook(lifeline, [target] { ++target->republished; });

        std::atomic<bool> destroyed{false};
        std::atomic<int> callsAfterDestroyed{0};
        Signal calledOnce;
        std::thread caller([&] {
            bool signalled = false;
            while (callsAfterDestroyed.load() < 100) {
                hook();
                if (!signalled) {
                    calledOnce.raise();
                    signalled = true;
                }
                if (destroyed.load()) ++callsAfterDestroyed;
            }
        });
        Joiner joinCaller{caller};

        calledOnce.wait();  // at least one call went through while alive
        retire(*lifeline);
        CHECK(target->republished >= 1);  // read after retire: ordered by the mutex
        delete target;
        destroyed = true;
        caller.join();
        CHECK(callsAfterDestroyed.load() == 100);
    }
}
