#include "simulator.hpp"
#include <filesystem>
#include <iostream>

namespace {
std::string schedulerName(int algorithm) {
    switch (algorithm) {
    case 0:
        return "Round-Robin";
    case 1:
        return "Shortest Job First";
    case 2:
        return "Shortest Remaining Time";
    case 3:
        return "Priority";
    default:
        return "First-Come First-Served";
    }
}

bool isJsonFile(const std::filesystem::path &path) {
    return path.extension() == ".json";
}
} // namespace

Simulator::Simulator(const std::string &configPath)
    : config(SystemConfig::loadFromFile(configPath)),
      memManager(config.main_memory.total, config.secondary_memory.total, config.cache.size,config.cache.line_size,config.main_memory.page_size),
      ioManager() {}

int Simulator::run() {
    std::cout << "Inicializando o simulador...\n";
    if (!loadProcesses()) {
        return 1;
    }
    memManager.setCacheReplacementPolicy(static_cast<ReplacementPolicy>(config.cache.policy)); //onde vai chamar pra trocar a politica de substituição da cache
    scheduler = std::make_unique<ProcessScheduler>(config.scheduling.algorithm, readyQueue);

    std::cout << "\nIniciando escalonador " << schedulerName(config.scheduling.algorithm) << "...\n";

    // Medir tempo de execução total
    auto startTime = std::chrono::high_resolution_clock::now();

    executeProcesses();
    
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedSeconds = endTime - startTime;
    std::cout << "\nTempo total de execução do simulador: " << elapsedSeconds.count() << " segundos.\n";

    std::cout << "\nTodos os processos foram finalizados. Encerrando o simulador.\n";
    return 0;
}

bool Simulator::loadProcesses() {
    const std::string tasksDir = "src/tasks";
    
    if (!std::filesystem::exists(tasksDir)) {
        std::cerr << "Erro: Diretório '" << tasksDir << "' não encontrado.\n";
        return false;
    }

    bool allLoaded = true;
    int processCount = 0;
    uint32_t nextBaseAddr = 0x00000000; // simple linear base; MemoryManager will map per-process

    try {
        for (const auto &entry : std::filesystem::directory_iterator(tasksDir)) {
            if (entry.is_regular_file() && isJsonFile(entry.path())) {
                std::string taskFile = entry.path().string();
                std::string taskLabel = entry.path().filename().string();

                std::cout << "Carregando task: " << taskLabel << "\n";
                
                // Assign unique PID and non-overlapping base address per task
                // Note: PCB pid is set inside loadProcessDefinition via the PCB instance
                allLoaded &= loadProcessDefinition(
                    taskLabel,
                    taskFile,
                    nextBaseAddr,
                    ++processCount
                );
                // // Advance base address by a page-aligned chunk to avoid overlap across tasks
                // // Assuming 4KB pages; bump by 0x10000 for generous separation
                nextBaseAddr += 0x00010000;
            }
        }

        if (processCount == 0) {
            std::cerr << "Aviso: Nenhum arquivo .json encontrado em '" << tasksDir << "'.\n";
            return false;
        }

        std::cout << "Total de " << processCount << " tasks carregadas com sucesso.\n";
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "Erro ao acessar diretório '" << tasksDir << "': " << e.what() << "\n";
        return false;
    }

    return allLoaded;
}

bool Simulator::loadProcessDefinition(
                                       const std::string &taskLabel,
                                       const std::string &taskFile,
                                       uint32_t baseAddress, 
                                       int pid) {
    auto process = std::make_unique<PCB>();
    process->pid = pid;
    
    std::cout << "Carregando programa '" << taskLabel << "' para o processo " << process->pid << "...\n";
    int startCodeAddr = loadJsonProgram(taskFile, memManager, *process, baseAddress);

    process->regBank.pc.write(startCodeAddr);
    process->memWeights.cache = static_cast<uint64_t>(config.cache.weight);
    process->memWeights.primary = static_cast<uint64_t>(config.main_memory.weight);
    process->memWeights.secondary = static_cast<uint64_t>(config.secondary_memory.weight);

    readyQueue.push_back(process.get());
    processList.push_back(std::move(process));
    return true;
}

void Simulator::moveUnblockedProcesses() {
    for (auto it = blockedQueue.begin(); it != blockedQueue.end();) {
        if ((*it)->state.load() == State::Ready) {
            std::cout << "[Scheduler] Processo " << (*it)->pid
                      << " desbloqueado e movido para a fila de prontos.\n";
            readyQueue.push_back(*it);
            it = blockedQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void Simulator::executeProcesses() {
    const int totalProcesses = static_cast<int>(processList.size());
    int finishedProcesses = 0;

    const int numCores = std::max(1, config.cpu.cores);
    std::vector<std::unique_ptr<CPUCore>> cpuCores;
    std::vector<PCB *> coreAssignments(numCores, nullptr);
    std::queue<int> idleCoresIdx;

    for (int i = 0; i < numCores; ++i) {
        cpuCores.push_back(std::make_unique<CPUCore>(i, memManager, ioManager));
        cpuCores.back()->start();
        cpuCores.back()->setSchedulingAlgorithm(config.scheduling.algorithm);
        idleCoresIdx.push(i);
    }

    while (finishedProcesses < totalProcesses) {
        moveUnblockedProcesses();
        reclaimFinishedCores(cpuCores, coreAssignments, idleCoresIdx, finishedProcesses);

        if (readyQueue.empty()) {
            reclaimFinishedCores(cpuCores, coreAssignments, idleCoresIdx, finishedProcesses);

            if (blockedQueue.empty() && readyQueue.empty() && allCoresIdle(coreAssignments)) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        PCB *currentProcess = scheduler->scheduler(readyQueue);
        readyQueue.erase(std::remove(readyQueue.begin(), readyQueue.end(), currentProcess), readyQueue.end());

        currentProcess->state.store(State::Running);

        while (idleCoresIdx.empty()) {
            reclaimFinishedCores(cpuCores, coreAssignments, idleCoresIdx, finishedProcesses);
            if (idleCoresIdx.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        int coreIdx = idleCoresIdx.front();
        idleCoresIdx.pop();
        coreAssignments[coreIdx] = currentProcess;
        cpuCores[coreIdx]->submitProcess(currentProcess, false);
        currentProcess->coresAssigned.push_back(coreIdx);
        {
            std::lock_guard<std::mutex> lock(printMutex);
            std::cout << "\n[Scheduler] Executando processo " << currentProcess->pid
                    << " (Quantum: " << currentProcess->quantum
                    << ") (Prioridade: " << currentProcess->priority << ")"
                    << ") (Intruções: " << currentProcess->instructions << ").\n";
        }
    }

    reclaimFinishedCores(cpuCores, coreAssignments, idleCoresIdx, finishedProcesses);

    for (auto &core : cpuCores) {
        core->stop();
    }
}

void Simulator::handleCompletion(PCB &process, int &finishedProcesses) {
    switch (process.state.load()) {
        case State::Blocked:{
            std::lock_guard<std::mutex> lock(blockedQueueMutex);
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cout << "[Scheduler] Processo " << process.pid
                        << " bloqueado por I/O. Entregando ao IOManager.\n";
            }
            ioManager.registerProcessWaitingForIO(&process);
            blockedQueue.push_back(&process);
            break;
        }    
        case State::Finished:{
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cout << "[Scheduler] Processo " << process.pid << " finalizado.\n";
                print_metrics(process);
            }
            memManager.freeProcessPages(process);
            finishedProcesses++;
            break;
        }
        default:{
            {
                std::lock_guard<std::mutex> lock(printMutex);
                std::cout << "[Scheduler] Quantum do processo " << process.pid
                        << " expirou. Voltando para a fila.\n";
            }
            std::lock_guard<std::mutex> lock(readyQueueMutex);
            process.state.store(State::Ready);
            readyQueue.push_back(&process);
            break;
        }
    }
}

void Simulator::reclaimFinishedCores(std::vector<std::unique_ptr<CPUCore>> &cpuCores,
                                     std::vector<PCB *> &coreAssignments,
                                     std::queue<int> &idleCoresIdx,
                                     int &finishedProcesses) {
    for (std::size_t idx = 0; idx < cpuCores.size(); ++idx) {
        PCB *assigned = coreAssignments[idx];
        if (!assigned) {
            continue;
        }
        if (!cpuCores[idx]->isIdle()) {
            continue;
        }

        handleCompletion(*assigned, finishedProcesses);
        coreAssignments[idx] = nullptr;
        idleCoresIdx.push(static_cast<int>(idx));
    }
}

bool Simulator::allCoresIdle(const std::vector<PCB *> &coreAssignments) const {
    return std::all_of(coreAssignments.begin(), coreAssignments.end(), [](PCB *pcb) {
        return pcb == nullptr;
    });
}