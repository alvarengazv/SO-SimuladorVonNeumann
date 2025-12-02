#ifndef CPU_CORE_HPP
#define CPU_CORE_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "MemoryManager.hpp"
#include "PCB.hpp"
#include "../IO/IOManager.hpp"

class CPUCore {
public:
    CPUCore(std::size_t coreId,
            MemoryManager &memManager,
            IOManager &ioManager);
    ~CPUCore();

    CPUCore(const CPUCore &) = delete;
    CPUCore &operator=(const CPUCore &) = delete;

    void start();
    void stop();

    void submitProcess(PCB *process, bool printLockState = true);

    bool isIdle() const;
    std::size_t id() const;

    void setSchedulingAlgorithm(int algorithm);

private:
    void workerLoop();
    void resetCurrentProcess();

    const std::size_t coreId;
    MemoryManager &memManager;
    IOManager &ioManager;

    std::thread workerThread;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};

    mutable std::mutex workMutex;
    std::condition_variable workCv;
    PCB *currentProcess{nullptr};
    bool currentPrintLock{true};
    std::vector<std::unique_ptr<IORequest>> ioRequestsBuffer;
    int schedulingAlgorithm = 0;
};

#endif