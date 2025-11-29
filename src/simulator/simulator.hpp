#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../system_config/system_config.hpp"
#include "../cpu/MemoryManager.hpp"
#include "../IO/IOManager.hpp"
#include "../cpu/PCB.hpp"
#include "../process_scheduler/process_scheduler.hpp"

class Simulator {
public:
    explicit Simulator(const std::string &configPath = "src/system_config/system_config.json");
    int run();

private:
    bool loadProcesses();
    bool loadProcessDefinition(const std::string &pcbFile,
                               const std::string &taskLabel,
                               const std::string &taskFile,
                               uint32_t baseAddress = 0);
    void moveUnblockedProcesses();
    void executeProcess(PCB &process, int &finishedProcesses);

    SystemConfig config;
    MemoryManager memManager;
    IOManager ioManager;

    std::vector<std::unique_ptr<PCB>> processList;
    std::vector<PCB *> readyQueue;
    std::vector<PCB *> blockedQueue;
    std::unique_ptr<ProcessScheduler> scheduler;
};

#endif // SIMULATOR_HPP
