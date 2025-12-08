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
        return "Lottery";
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
      memManager(config.main_memory.total, config.secondary_memory.total, config.cache.size,config.cache.line_size,config.main_memory.page_size,static_cast<PolicyType>(config.main_memory.policy)),
      ioManager() {}

int Simulator::run() {
    std::cout << "Inicializando o simulador...\n";
    if (!loadProcesses()) {
        return 1;
    }
    memManager.setCacheReplacementPolicy(static_cast<PolicyType>(config.cache.policy)); //onde vai chamar pra trocar a politica de substituição da cache
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
    uint32_t nextBaseAddr = 0x00000000;

    try {
        for (const auto &entry : std::filesystem::directory_iterator(tasksDir)) {
            if (entry.is_regular_file() && isJsonFile(entry.path())) {
                std::string taskFile = entry.path().string();
                std::string taskLabel = entry.path().filename().string();

                std::cout << "Carregando task: " << taskLabel << "\n";
                
                allLoaded &= loadProcessDefinition(
                    taskLabel,
                    taskFile,
                    nextBaseAddr,
                    ++processCount
                );
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
    PCB::registerProcess(process.get());
    
    std::cout << "Carregando programa '" << taskLabel << "' para o processo " << process->pid << "...\n";
    int startCodeAddr = loadJsonProgram(taskFile, memManager, *process, baseAddress);

    process->regBank.pc.write(startCodeAddr);
    process->memWeights.cache = static_cast<uint64_t>(config.cache.weight);
    process->memWeights.primary = static_cast<uint64_t>(config.main_memory.weight);
    process->memWeights.secondary = static_cast<uint64_t>(config.secondary_memory.weight);
    
    process->arrivalTime.store(process->timeStamp);

    readyQueue.push_back(process.get());
    processList.push_back(std::move(process));
    return true;
}

void Simulator::moveUnblockedProcesses() {
    for (auto it = blockedQueue.begin(); it != blockedQueue.end();) {
        if ((*it)->state.load() == State::Ready) {
            // std::cout << "[Scheduler] Processo " << (*it)->pid
            //           << " desbloqueado e movido para a fila de prontos.\n";
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
        collectMemoryMetrics(); // Coleta métricas a cada iteração do loop principal
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
        // {
        //     std::lock_guard<std::mutex> lock(printMutex);
        //     // std::cout << "\n[Scheduler] Executando processo " << currentProcess->pid
        //     //         << " (Quantum: " << currentProcess->quantum
        //     //         << ") (Prioridade: " << currentProcess->priority << ")"
        //     //         << ") (Intruções: " << currentProcess->instructions << ").\n";
        // }
    }

    reclaimFinishedCores(cpuCores, coreAssignments, idleCoresIdx, finishedProcesses);
    
    saveMemoryMetrics();

    for (auto &core : cpuCores) {
        core->stop();
    }

    for(auto process : finishedQueue) {
        print_metrics(*process);
        memManager.freeProcessPages(*process);
    }

    uint64_t totalBurstTime = 0;     // soma do tempo de CPU consumido
    uint64_t totalTurnaround = 0;    // soma do turnaround
    uint64_t totalWaiting = 0;       // soma do waiting time

    for (auto process : finishedQueue) {
        totalBurstTime += process->burstTime.load();
        totalTurnaround += process->turnaroundTime.load();
        totalWaiting += process->waitingTime.load();
    }

    uint64_t totalSimTime = 0;
    for (auto process : finishedQueue) {
        if (process->finishTime.load() > totalSimTime)
            totalSimTime = process->finishTime.load();
    }

    size_t n = finishedQueue.size();
    double avgWaitingTime = static_cast<double>(totalWaiting) / n;
    double avgTurnaroundTime = static_cast<double>(totalTurnaround) / n;

    // Utilização média da CPU (fração do tempo útil sobre a capacidade total)
    // totalBurstTime é a soma do tempo de CPU consumido por todos os processos.
    // Em sistemas com múltiplos núcleos a capacidade total disponível é
    // numCores * totalSimTime. Sem dividir por numCores a métrica pode
    // exceder 1.0 (100%).
    double cpuUtilization = 0.0;
    if (totalSimTime > 0) {
        cpuUtilization = static_cast<double>(totalBurstTime) / (static_cast<double>(numCores) * static_cast<double>(totalSimTime));
    }

    // Eficiência (CPU útil / tempo total disponível). Mantemos igual à
    // utilização aqui, mas sem a normalização por núcleos o valor seria >100%.
    double efficiency = cpuUtilization;

    // Throughput global (processos concluídos / tempo total)
    double throughput = static_cast<double>(n) / totalSimTime;

    std::cout << "\n=== MÉTRICAS GLOBAIS DO SIMULADOR ===\n";
    std::cout << "Tempo médio de espera: " << avgWaitingTime << " ciclos\n";
    std::cout << "Tempo médio de execução: " << avgTurnaroundTime << " ciclos\n";
    std::cout << "Utilização média da CPU: " << cpuUtilization * 100 << " %\n";
    std::cout << "Eficiência: " << efficiency * 100 << " %\n";
    std::cout << "Throughput global: " << throughput << " processos/ciclo\n";
}

void Simulator::handleCompletion(PCB &process, int &finishedProcesses) {
    switch (process.state.load()) {
        case State::Blocked:{
            std::lock_guard<std::mutex> lock(blockedQueueMutex);
            // {
            //     std::lock_guard<std::mutex> lock(printMutex);
            //     // std::cout << "[Scheduler] Processo " << process.pid
            //     //         << " bloqueado por I/O. Entregando ao IOManager.\n";
            // }
            ioManager.registerProcessWaitingForIO(&process);
            blockedQueue.push_back(&process);
            break;
        }    
        case State::Finished:{
            
            // Calcula métricas finais do processo
            auto finish = std::chrono::high_resolution_clock::now(); // se quiser tempo real

            // Aqui assumimos que você já tem timeStamp como "ciclo final" do processo
            process.finishTime.store(process.timeStamp);

            // Turnaround Time = finishTime - arrivalTime
            process.turnaroundTime.store(process.finishTime.load() - process.arrivalTime.load());

            // Waiting Time = turnaroundTime - burstTime
            process.waitingTime.store(process.turnaroundTime.load() - process.burstTime.load());

            // Response Time = startTime - arrivalTime
            process.responseTime.store(process.startTime.load() - process.arrivalTime.load());


            {
                std::lock_guard<std::mutex> lock(finishedQueueMutex);
                finishedQueue.push_back(&process);
            }
            finishedProcesses++;
            // {
            //     std::lock_guard<std::mutex> lock(printMutex);
            //     std::cout << "[Scheduler] Processo " << process.pid << " finalizado.\n";
            // }
            break;
        }
        default:{
            // {
            //     std::lock_guard<std::mutex> lock(printMutex);
            //     // std::cout << "[Scheduler] Quantum do processo " << process.pid
            //     //         << " expirou. Voltando para a fila.\n";
            //     // cout << "\n\n\n Iniciando preempção de processos \n\n\n";

            // }
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

void Simulator::collectMemoryMetrics() {
    using namespace std::chrono;
    auto now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    
    MemoryUsageRecord record;
    record.timestamp = now;

    size_t cacheUsed = memManager.getCacheUsage();
    size_t cacheTotal = memManager.getCacheCapacity();
    record.cacheUsage = (cacheTotal > 0) ? (static_cast<double>(cacheUsed) / cacheTotal * 100.0) : 0.0;

    size_t ramUsed = memManager.getMainMemoryUsage();
    size_t ramTotal = memManager.totalFrames;
    record.ramUsage = (ramTotal > 0) ? (static_cast<double>(ramUsed) / ramTotal * 100.0) : 0.0;

    size_t diskUsed = memManager.getSecondaryMemoryUsage();
    size_t diskTotal = memManager.getSecondaryMemoryCapacity();
    record.diskUsage = (diskTotal > 0) ? (static_cast<double>(diskUsed) / diskTotal * 100.0) : 0.0;
    
    memoryUsageHistory.push_back(record);
}

void Simulator::saveMemoryMetrics() {
    ReplacementPolicy rP;
    // Informações da memória primaria
    size_t primaryMemorySize = config.main_memory.total;
    size_t primaryMemoryPageSize = config.main_memory.page_size;
    std::string primaryMemoryPolicy = rP.getType() == PolicyType::FIFO ? "FIFO" : "LRU";

    // Informações da memória secundária
    size_t secondaryMemorySize = config.secondary_memory.total;
    size_t secondaryMemoryBlockSize = config.secondary_memory.block_size;

    // Informações da cache
    size_t cacheSize = config.cache.size;
    size_t cacheLineSize = config.cache.line_size;
    std::string cachePolicy = rP.getType() == PolicyType::FIFO ? "FIFO" : "LRU";

    // Número de Cores
    size_t numCores = config.cpu.cores;

    // Escalonador
    std::string schedulerAlgorithm = schedulerName(config.scheduling.algorithm);

    const std::string filename = "output/memory_usage.csv";
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Erro ao abrir arquivo para salvar métricas de memória: " << filename << "\n";
        return;
    }

    file << "Timestamp,CacheUsage(%),RAMUsage(%),DiskUsage(%),PrimaryMemorySize,PrimaryMemoryPageSize,PrimaryMemoryPolicy,SecondaryMemorySize,SecondaryMemoryBlockSize,CacheSize,CacheLineSize,CachePolicy,NumCores,Scheduler\n";
    
    if (memoryUsageHistory.empty()) return;

    long long startTime = memoryUsageHistory.front().timestamp;

    for (const auto& record : memoryUsageHistory) {
        file << (record.timestamp - startTime) << ","
             << record.cacheUsage << ","
             << record.ramUsage << ","
             << record.diskUsage << ","
             << primaryMemorySize << ","
             << primaryMemoryPageSize << ","
             << primaryMemoryPolicy << ","
             << secondaryMemorySize << ","
             << secondaryMemoryBlockSize << ","
             << cacheSize << ","
             << cacheLineSize << ","
             << cachePolicy << ","
             << numCores << ","
             << schedulerAlgorithm << "\n";
    }
    
    std::cout << "Métricas de memória salvas em: " << filename << "\n";
}