#include "simulator.hpp"

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
} // namespace

Simulator::Simulator(const std::string &configPath)
    : config(SystemConfig::loadFromFile(configPath)),
      memManager(config.main_memory.total, config.secondary_memory.total, config.cache.size,config.cache.line_size),
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
    struct ProcessDefinition {
        std::string pcbFile;
        std::string taskLabel;
        std::string taskFile;
        uint32_t baseAddress;
    };

    const std::vector<ProcessDefinition> definitions = {
        {"src/pcbs/process1.json", "tasks.json", "src/tasks/tasks.json", 0x00000000},
        {"src/pcbs/process2.json", "tasks_counter.json", "src/tasks/tasks_counter.json", 0x00000100},
        {"src/pcbs/process3.json", "tasks_io.json", "src/tasks/tasks_io.json", 0x00000200},
        {"src/pcbs/process_forward.json", "tasks_forward.json", "src/tasks/tasks_forward.json", 0x00000300}};

    bool allLoaded = true;
    for (const auto &definition : definitions) {
        allLoaded &= loadProcessDefinition(definition.pcbFile,
                           definition.taskLabel,
                           definition.taskFile,
                           definition.baseAddress);
    }
    return allLoaded;
}

bool Simulator::loadProcessDefinition(const std::string &pcbFile,
                                       const std::string &taskLabel,
                                       const std::string &taskFile,
                                       uint32_t baseAddress) {
    auto process = std::make_unique<PCB>();
    if (!load_pcb_from_json(pcbFile, *process)) {
        std::cerr << "Erro ao carregar '" << pcbFile
                  << "'. Certifique-se de que o arquivo está na pasta raiz do projeto.\n";
        return false;
    }

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
        cpuCores[coreIdx]->submitProcess(currentProcess);
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