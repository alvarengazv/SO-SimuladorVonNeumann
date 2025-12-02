#include "core.hpp"

#include "CONTROL_UNIT.hpp"

namespace {
std::atomic<bool> defaultPrintLock{true};
}

CPUCore::CPUCore(std::size_t coreId,
                 MemoryManager &memManager,
                 IOManager &ioManager)
    : coreId(coreId),
      memManager(memManager),
      ioManager(ioManager) {}

CPUCore::~CPUCore() {
    stop();
}

void CPUCore::start() {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return;
    }
    stopRequested.store(false);
    workerThread = std::thread(&CPUCore::workerLoop, this);
}

void CPUCore::stop() {
    if (!running.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(workMutex);
        stopRequested.store(true);
    }
    workCv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
    running.store(false);
}

void CPUCore::submitProcess(PCB *process, bool printLockState) {
    if (process == nullptr) {
        return;
    }

    std::unique_lock<std::mutex> lock(workMutex);
    workCv.wait(lock, [&]() {
        return stopRequested.load() || currentProcess == nullptr;
    });

    if (stopRequested.load()) {
        return;
    }

    currentProcess = process;
    currentPrintLock = printLockState;
    ioRequestsBuffer.clear();
    lock.unlock();
    workCv.notify_one();
}

bool CPUCore::isIdle() const {
    std::lock_guard<std::mutex> lock(workMutex);
    return currentProcess == nullptr;
}

std::size_t CPUCore::id() const {
    return coreId;
}

void CPUCore::workerLoop() {
    while (true) {
        PCB *process = nullptr;
        bool printLockState = true;
        {
            std::unique_lock<std::mutex> lock(workMutex);
            workCv.wait(lock, [&]() {
                return stopRequested.load() || currentProcess != nullptr;
            });

            if (stopRequested.load() && currentProcess == nullptr) {
                break;
            }

            process = currentProcess;
            printLockState = currentPrintLock;
        }

        if (!process) {
            continue;
        }

        ioRequestsBuffer.clear();
        std::atomic<bool> printLock(printLockState);
        Core(memManager, *process, &ioRequestsBuffer, printLock);

        {
            std::lock_guard<std::mutex> lock(workMutex);
            currentProcess = nullptr;
            currentPrintLock = true;
        }
        workCv.notify_all();
    }
}

void CPUCore::resetCurrentProcess() {
    std::lock_guard<std::mutex> lock(workMutex);
    currentProcess = nullptr;
    currentPrintLock = true;
    ioRequestsBuffer.clear();
    workCv.notify_all();
}
