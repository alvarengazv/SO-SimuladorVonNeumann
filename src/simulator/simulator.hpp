#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <atomic>
#include <queue>
#include <ctime>
#include <filesystem>
#include <stdexcept>

#include "../cpu/CONTROL_UNIT.hpp"
#include "../metrics/metrics.hpp"
#include "../parser_json/parser_json.hpp"
#include "../system_config/system_config.hpp"
#include "../cpu/MemoryManager.hpp"
#include "../IO/IOManager.hpp"
#include "../cpu/PCB.hpp"
#include "../memory/replacement_police.hpp"
#include "../process_scheduler/process_scheduler.hpp"
#include "../cpu/core.hpp"

class Simulator {
public:
    explicit Simulator(const std::string &configPath = "src/system_config/system_config.json");
    int run();

private:
    bool loadProcesses();
    bool loadProcessDefinition(
                               const std::string &taskLabel,
                               const std::string &taskFile,
                               uint32_t baseAddress = 0,
                               int pid = 0);
    void moveUnblockedProcesses();
    void executeProcesses();
    void handleCompletion(PCB &process, int &finishedProcesses);
    void reclaimFinishedCores(std::vector<std::unique_ptr<CPUCore>> &cpuCores,
                              std::vector<PCB *> &coreAssignments,
                              std::queue<int> &idleCoresIdx,
                              int &finishedProcesses);
    bool allCoresIdle(const std::vector<PCB *> &coreAssignments) const;

    SystemConfig config;
    MemoryManager memManager;

    std::vector<std::unique_ptr<PCB>> processList;
    std::vector<PCB *> readyQueue;
    mutable std::mutex readyQueueMutex;
    std::vector<PCB *> blockedQueue;
    mutable std::mutex blockedQueueMutex;
    std::vector<PCB *> finishedQueue;
    mutable std::mutex finishedQueueMutex;
    std::unique_ptr<ProcessScheduler> scheduler;
    mutable std::mutex printMutex;
    IOManager ioManager;
};

#endif // SIMULATOR_HPP
