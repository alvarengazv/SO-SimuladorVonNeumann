#ifndef PCB_HPP
#define PCB_HPP
/*
  PCB.hpp
  Definição do bloco de controle de processo (PCB) usado pelo simulador da CPU.
  Centraliza: identificação do processo, prioridade, quantum, pesos de memória e
  contadores de instrumentação de pipeline/memória.
*/
#include <string>
#include <atomic>
#include <cstdint>
#include <vector>
#include <mutex>
#include "cache/cache.hpp"
#include "datapath/REGISTER_BANK.hpp" // necessidade de objeto completo dentro do PCB


// Estados possíveis do processo (simplificado)
enum class State {
    Ready,
    Running,
    Blocked,
    Finished
};

struct MemWeights {
    uint64_t cache = 1;   // custo por acesso à memória cache
    uint64_t primary = 5; // custo por acesso à memória primária
    uint64_t secondary = 10; // custo por acesso à memória secundária
};

struct PCB {
    int pid = 0;
    int coreAssigned = -1;
    std::string name;
    int quantum = 0;
    int timeStamp = 0;
    int priority = 0;
    int instructions;

    std::atomic<State> state{State::Ready};
    hw::REGISTER_BANK regBank;

    // Contadores de acesso à memória
    std::atomic<uint64_t> primary_mem_accesses{0};
    std::atomic<uint64_t> secondary_mem_accesses{0};
    std::atomic<uint64_t> memory_cycles{0};
    std::atomic<uint64_t> mem_accesses_total{0};
    std::atomic<uint64_t> extra_cycles{0};
    std::atomic<uint64_t> cache_mem_accesses{0};

    // Instrumentação detalhada
    std::atomic<uint64_t> pipeline_cycles{0};
    std::atomic<uint64_t> stage_invocations{0};
    std::atomic<uint64_t> mem_reads{0};
    std::atomic<uint64_t> mem_writes{0};

    // Novos contadores
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    std::atomic<uint64_t> io_cycles{1};

    MemWeights memWeights;

    // Saída lógica gerada pelo programa (ex.: instruções PRINT)
    std::vector<std::string> programOutput;
    mutable std::mutex outputMutex;

    void appendProgramOutput(const std::string &line) {
        std::lock_guard<std::mutex> lock(outputMutex);
        programOutput.push_back(line);
    }

    std::vector<std::string> snapshotProgramOutput() const {
        std::lock_guard<std::mutex> lock(outputMutex);
        return programOutput;
    }

    int totalTimeExecution() const {
        return (timeStamp + memory_cycles.load() + io_cycles.load());
    }
};

// Contabilizar cache
inline void contabiliza_cache(PCB &pcb, bool hit) {
    if (hit) {
        pcb.cache_hits++;
    } else {
        pcb.cache_misses++;
    }
}

#endif // PCB_HPP