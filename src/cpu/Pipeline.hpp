#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

#include "MemoryManager.hpp"
#include "datapath/REGISTER_BANK.hpp"
#include "../IO/IOManager.hpp"
#include "CONTROL_UNIT.hpp"
#include "GlobalClock.hpp"

class Pipeline {
public:
    Pipeline(int pipelineId = 0);
    ~Pipeline();

    void run(MemoryManager &memoryManager, PCB &process, vector<unique_ptr<IORequest>>* ioRequests, std::atomic<bool> &printLock, int schedulerId);
    void stop();
    
    // Get the pipeline ID
    int getPipelineId() const { return pipelineId_; }

private:
    // Pipeline ID for GlobalClock registration
    int pipelineId_;
    
    // Pipeline components
    Control_Unit UC;
    PipelineRegister ifId;
    PipelineRegister idEx;
    PipelineRegister exMem;
    PipelineRegister memWb;

    // Threads
    std::thread fetchThread;
    std::thread decodeThread;
    std::thread executeThread;
    std::thread memoryThread;
    std::thread writeThread;

    // Synchronization
    std::mutex mutex;
    std::condition_variable cv_start;
    std::condition_variable cv_done;
    
    bool stop_threads = false;
    int work_generation = 0;
    int active_threads = 0;
    
    // Clock synchronization mode
    bool useGlobalClock_ = true;

    // Context for the current run (pointer to stack variable in run())
    ControlContext* currentContext = nullptr;

    // Worker loops
    void fetchLoop();
    void decodeLoop();
    void executeLoop();
    void memoryLoop();
    void writeLoop();
    
    // Helper to record cycle metrics
    void recordStageCycle(PCB& process, PipelineStage stage, uint64_t cycle);
};

#endif // PIPELINE_HPP