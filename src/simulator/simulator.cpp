#include "simulator.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "../cpu/CONTROL_UNIT.hpp"
#include "../cpu/pcb_loader.hpp"
#include "../metrics/metrics.hpp"
#include "../parser_json/parser_json.hpp"

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
      memManager(config.main_memory.total, config.secondary_memory.total),
      ioManager() {}

int Simulator::run() {
    std::cout << "Inicializando o simulador...\n";
    if (!loadProcesses()) {
        return 1;
    }
    memManager.setCacheReplacementPolicy(config.cache.policy); //onde vai chamar pra trocar a politica de substituição da cache
    scheduler = std::make_unique<ProcessScheduler>(config.scheduling.algorithm, readyQueue);

    const int totalProcesses = static_cast<int>(processList.size());
    int finishedProcesses = 0;

    std::cout << "\nIniciando escalonador " << schedulerName(config.scheduling.algorithm) << "...\n";

    while (finishedProcesses < totalProcesses) {
        moveUnblockedProcesses();

        if (readyQueue.empty()) {
            if (blockedQueue.empty()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        PCB *currentProcess = scheduler->scheduler(readyQueue);
        readyQueue.erase(std::remove(readyQueue.begin(), readyQueue.end(), currentProcess), readyQueue.end());

        executeProcess(*currentProcess, finishedProcesses);
    }

    std::cout << "\nTodos os processos foram finalizados. Encerrando o simulador.\n";
    return 0;
}

bool Simulator::loadProcesses() {
    struct ProcessDefinition {
        std::string pcbFile;
        std::string taskLabel;
        std::string taskFile;
    };

    const std::vector<ProcessDefinition> definitions = {
        {"src/pcbs/process1.json", "tasks.json", "src/tasks/tasks.json"},
        {"src/pcbs/process2.json", "tasks_counter.json", "src/tasks/tasks_counter.json"},
        {"src/pcbs/process3.json", "tasks_io.json", "src/tasks/tasks_io.json"}};

    bool allLoaded = true;
    for (const auto &definition : definitions) {
        allLoaded &= loadProcessDefinition(definition.pcbFile,
                                           definition.taskLabel,
                                           definition.taskFile,
                                           0);
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
    loadJsonProgram(taskFile, memManager, *process, baseAddress);

    readyQueue.push_back(process.get());
    processList.push_back(std::move(process));
    return true;
}

void Simulator::moveUnblockedProcesses() {
    for (auto it = blockedQueue.begin(); it != blockedQueue.end();) {
        if ((*it)->state == State::Ready) {
            std::cout << "[Scheduler] Processo " << (*it)->pid
                      << " desbloqueado e movido para a fila de prontos.\n";
            readyQueue.push_back(*it);
            it = blockedQueue.erase(it);
        } else {
            ++it;
        }
    }
}

void Simulator::executeProcess(PCB &process, int &finishedProcesses) {
    std::cout << "\n[Scheduler] Executando processo " << process.pid
              << " (Quantum: " << process.quantum
              << ") (Prioridade: " << process.priority << ")"
              << ") (Intruções: " << process.instructions << ").\n";
    process.state = State::Running;

    std::vector<std::unique_ptr<IORequest>> ioRequests;
    bool printLock = true;

    Core(memManager, process, &ioRequests, printLock);

    switch (process.state) {
    case State::Blocked:
        std::cout << "[Scheduler] Processo " << process.pid
                  << " bloqueado por I/O. Entregando ao IOManager.\n";
        ioManager.registerProcessWaitingForIO(&process);
        blockedQueue.push_back(&process);
        break;

    case State::Finished:
        std::cout << "[Scheduler] Processo " << process.pid << " finalizado.\n";
        print_metrics(process);
        finishedProcesses++;
        break;

    default:
        std::cout << "[Scheduler] Quantum do processo " << process.pid
                  << " expirou. Voltando para a fila.\n";
        process.state = State::Ready;
        readyQueue.push_back(&process);
        break;
    }
}
