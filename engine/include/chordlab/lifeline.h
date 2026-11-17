#pragma once

#include "chordlab/link.h"

#include <functional>
#include <memory>
#include <mutex>

namespace chordlab {

// What protects a call aimed at an instance from outliving it (per-track instruments plan I6). The shell's
// main-thread hops (`OnMain`) and the republish hooks other instances run (`LinkRegistry::Hook`) hold a
// `shared_ptr<Lifeline>`, never the instance alone: they take `mutex`, act only while `alive`, and so either run
// to the end before the instance's destructor gets past `retire`, or never run at all. Lock order: `mutex` is
// taken before the instance's model mutex, which is taken before the registry's (a leaf). The mutex is not
// recursive: a call that already holds it must never call `retire` or a guarded hook of the same lifeline.

/// What lets a call that was queued or handed to another instance find out that its target is gone.
struct Lifeline {
    std::mutex mutex;
    bool alive = true;
};

/// A republish hook for `LinkRegistry` / `LinkEndpoint`: takes `lifeline->mutex` and calls `republish` only while
/// `alive`. It holds the lifeline, never the instance, so it is safe to call after the instance is gone: a hook
/// copied into some other instance's `LinkRegistry::Hooks` keeps the lifeline alive (not the instance), and once
/// `retire` returned it does nothing. Safe to call from any thread, concurrently with itself and with `retire`.
/// An empty hook when `lifeline` is null or `republish` is empty (the registry never returns an empty hook).
LinkRegistry::Hook guardedHook(std::shared_ptr<Lifeline> lifeline, std::function<void()> republish);

/// Sets `alive` false under the mutex: when it returns, no guarded call is running and none will run. Waits for a
/// guarded call in progress (on another thread) to finish. Idempotent. Must not be called by a thread that holds
/// `lifeline.mutex` (from inside a guarded call): it would deadlock.
void retire(Lifeline& lifeline);

}  // namespace chordlab
