#include "chordlab/lifeline.h"

#include <utility>

namespace chordlab {

LinkRegistry::Hook guardedHook(std::shared_ptr<Lifeline> lifeline, std::function<void()> republish) {
    if (!lifeline || !republish) {
        return {};
    }
    return [lifeline = std::move(lifeline), republish = std::move(republish)] {
        const std::lock_guard<std::mutex> guard(lifeline->mutex);
        if (lifeline->alive) {
            republish();
        }
    };
}

void retire(Lifeline& lifeline) {
    const std::lock_guard<std::mutex> guard(lifeline.mutex);
    lifeline.alive = false;
}

}  // namespace chordlab
