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

struct PageTableEntry {
    uint32_t frameNumber; 
    bool valid;           
    bool dirty;          
};

// Cycle-accurate metrics for detailed analysis
struct CycleMetrics {
    // Cycle counts (sum of all stage cycles - equals 5 * instructions for 5-stage pipeline)
    std::atomic<uint64_t> totalCycles{0};           // Total stage-cycles consumed
    std::atomic<uint64_t> fetchCycles{0};           // Cycles spent in fetch stage
    std::atomic<uint64_t> decodeCycles{0};          // Cycles spent in decode stage
    std::atomic<uint64_t> executeCycles{0};         // Cycles spent in execute stage
    std::atomic<uint64_t> memoryCycles{0};          // Cycles spent in memory stage
    std::atomic<uint64_t> writebackCycles{0};       // Cycles spent in writeback stage
    
    // Wall-clock cycles (actual clock ticks while this process was active)
    std::atomic<uint64_t> wallClockCycles{0};       // True elapsed cycles
    
    // Stall tracking
    std::atomic<uint64_t> stallCycles{0};           // Total cycles wasted due to stalls
    std::atomic<uint64_t> fetchStalls{0};           // Stalls waiting for branch resolution
    std::atomic<uint64_t> decodeStalls{0};          // Stalls waiting for data hazards
    std::atomic<uint64_t> memoryStalls{0};          // Stalls waiting for memory
    
    // Instruction tracking
    std::atomic<uint64_t> instructionsCompleted{0}; // Instructions that reached WB
    std::atomic<uint64_t> instructionsFlushed{0};   // Instructions flushed due to branch
    
    // Start/end timestamps
    uint64_t startCycle{0};                         // First cycle of execution
    uint64_t endCycle{0};                           // Last cycle of execution
    
    // Wall-clock cycles (actual elapsed cycles from GlobalClock)
    // In a 5-stage pipeline: wall-clock = N_instructions + 4 (fill time)
    uint64_t getWallClockCycles() const {
        uint64_t wall = wallClockCycles.load();
        if (wall > 0) {
            // wallClockCycles counts fetches, add 4 for pipeline drain (last instruction
            // takes 4 more cycles to complete after its fetch)
            return wall + 4;
        }
        // Fallback to endCycle - startCycle if available
        if (endCycle > startCycle) return endCycle - startCycle;
        // Last resort: instructions + 4
        uint64_t completed = instructionsCompleted.load();
        return (completed > 0) ? (completed + 4) : 0;
    }
    
    // CPI calculation using wall-clock cycles (true pipeline efficiency)
    double getCPI() const {
        uint64_t completed = instructionsCompleted.load();
        if (completed == 0) return 0.0;
        uint64_t wallClock = getWallClockCycles();
        return static_cast<double>(wallClock) / static_cast<double>(completed);
    }
    
    // IPC calculation using wall-clock cycles
    double getIPC() const {
        uint64_t wallClock = getWallClockCycles();
        if (wallClock == 0) return 0.0;
        return static_cast<double>(instructionsCompleted.load()) / static_cast<double>(wallClock);
    }
    
    // Stage-based CPI (always 5.0 for 5-stage pipeline - useful for validation)
    double getStageCPI() const {
        uint64_t completed = instructionsCompleted.load();
        if (completed == 0) return 0.0;
        return static_cast<double>(totalCycles.load()) / static_cast<double>(completed);
    }
    
    // Stage utilization
    double getStageUtilization(uint64_t stageCycles) const {
        uint64_t total = totalCycles.load();
        if (total == 0) return 0.0;
        return static_cast<double>(stageCycles) / static_cast<double>(total) * 100.0;
    }
};

struct PCB {
    int pid = 0;
    std::vector<int> coresAssigned;
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

    std::unordered_map<uint32_t, PageTableEntry> pageTable;

    MemWeights memWeights;
    
    // Cycle-accurate metrics from GlobalClock
    CycleMetrics cycleMetrics;

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
    
    // Cycle-accurate total execution time
    uint64_t totalCycleExecution() const {
        return cycleMetrics.totalCycles.load();
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