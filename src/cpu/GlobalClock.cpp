#include "GlobalClock.hpp"
#include <iostream>

GlobalClock& GlobalClock::instance() {
    static GlobalClock instance;
    return instance;
}

void GlobalClock::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    cycle_.store(0, std::memory_order_release);
    targetCycle_ = 1;
    completedStages_.clear();
}

void GlobalClock::registerPipeline(int pipelineId, int numStages) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < numStages; ++i) {
        int stageKey = pipelineId * STAGE_COUNT + i;
        registeredStages_.insert(stageKey);
    }
    pipelineCount_++;
}

void GlobalClock::unregisterPipeline(int pipelineId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < STAGE_COUNT; ++i) {
        int stageKey = pipelineId * STAGE_COUNT + i;
        registeredStages_.erase(stageKey);
        completedStages_.erase(stageKey);
    }
    if (pipelineCount_ > 0) pipelineCount_--;
    
    cycleEndCv_.notify_all();
    cycleStartCv_.notify_all();
}

uint64_t GlobalClock::waitForCycleStart(int pipelineId, int stageId) {
    // In this simplified model, we don't actually block waiting for the cycle.
    // Instead, we just return the current cycle and let the stage proceed.
    // The tick() is called by each stage after completing work.
    return cycle_.load(std::memory_order_acquire);
}

void GlobalClock::stageComplete(int pipelineId, int stageId) {
    // In this model, completing a stage advances the global clock by 1.
    // This gives us cycle counting without true lock-step synchronization.
    cycle_.fetch_add(1, std::memory_order_release);
}

void GlobalClock::tryAdvanceCycle() {
    // Not used in the simplified model
}

bool GlobalClock::allStagesComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completedStages_ == registeredStages_;
}

int GlobalClock::totalRegisteredStages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(registeredStages_.size());
}

void GlobalClock::tick() {
    cycle_.fetch_add(1, std::memory_order_release);
}

void GlobalClock::pause() {
    paused_.store(true, std::memory_order_release);
}

void GlobalClock::resume() {
    paused_.store(false, std::memory_order_release);
    cycleStartCv_.notify_all();
}
