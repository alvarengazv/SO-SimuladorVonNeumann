#ifndef GLOBAL_CLOCK_HPP
#define GLOBAL_CLOCK_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <cstdint>
#include <functional>
#include <vector>

// Stage IDs for pipeline synchronization
enum PipelineStage {
    STAGE_FETCH = 0,
    STAGE_DECODE = 1,
    STAGE_EXECUTE = 2,
    STAGE_MEMORY = 3,
    STAGE_WRITEBACK = 4,
    STAGE_COUNT = 5
};

/**
 * GlobalClock - Cycle-accurate clock for pipeline simulation
 * 
 * This clock provides barrier-based synchronization for pipeline stages.
 * All stages must complete their work before the clock advances to the next cycle.
 * 
 * Usage:
 *   1. Each pipeline registers its stages with registerPipeline()
 *   2. Stages call waitForCycleStart() at the beginning of each cycle
 *   3. Stages call stageComplete() when done with their work
 *   4. The clock automatically advances when all stages complete
 */
class GlobalClock {
public:
    // Get singleton instance
    static GlobalClock& instance();
    
    // Get current cycle number
    uint64_t currentCycle() const;
    
    // Reset clock to cycle 0
    void reset();
    
    // Register a pipeline's stages for synchronization
    // pipelineId: unique identifier for this pipeline
    // numStages: number of stages (typically 5)
    void registerPipeline(int pipelineId, int numStages = STAGE_COUNT);
    
    // Unregister a pipeline (call when pipeline finishes or stops)
    void unregisterPipeline(int pipelineId);
    
    // Wait for the start of a new cycle (blocks until clock advances)
    // Returns the new cycle number
    uint64_t waitForCycleStart(int pipelineId, int stageId);
    
    // Signal that a stage has completed its work for this cycle
    void stageComplete(int pipelineId, int stageId);
    
    // Check if all stages have completed (for debugging/metrics)
    bool allStagesComplete() const;
    
    // Get total number of registered stages
    int totalRegisteredStages() const;
    
    // Manual tick (for testing or single-threaded mode)
    void tick();
    
    // Pause/resume clock (useful for debugging)
    void pause();
    void resume();
    bool isPaused() const;

private:
    GlobalClock() = default;
    ~GlobalClock() = default;
    GlobalClock(const GlobalClock&) = delete;
    GlobalClock& operator=(const GlobalClock&) = delete;
    
    void tryAdvanceCycle();
    
    std::atomic<uint64_t> cycle_{0};
    std::atomic<bool> paused_{false};
    
    mutable std::mutex mutex_;
    std::condition_variable cycleStartCv_;
    std::condition_variable cycleEndCv_;
    
    // Tracking which stages are registered and which have completed
    // Key: (pipelineId * STAGE_COUNT + stageId)
    std::set<int> registeredStages_;
    std::set<int> completedStages_;
    
    // The cycle that stages are currently waiting for
    uint64_t targetCycle_{1};
    
    // Number of pipelines registered
    int pipelineCount_{0};
};

// Inline implementation for performance-critical methods
inline uint64_t GlobalClock::currentCycle() const {
    return cycle_.load(std::memory_order_acquire);
}

inline bool GlobalClock::isPaused() const {
    return paused_.load(std::memory_order_acquire);
}

#endif // GLOBAL_CLOCK_HPP