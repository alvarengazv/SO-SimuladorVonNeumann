#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <memory>
#include <stdexcept>
#include <mutex>
#include "../memory/MAIN_MEMORY.hpp"
#include "../memory/SECONDARY_MEMORY.hpp"
#include "../memory/replacement_police.hpp"
#include "cache/cache.hpp"
#include "PCB.hpp"

// Forward declarations para evitar ciclo de includes
class PCB;
class Cache;

class MemoryManager
{
public:
    size_t pageSize;
    size_t totalFrames;
    std::vector<bool> framesBitmap;

    MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheNumLines, size_t cacheLineSizeBytes, size_t pageSize);

    // Métodos unificados agora recebem o PCB para as métricas
    uint32_t read(uint32_t LogicalAddress, PCB &process);
    void write(uint32_t LogicalAddress, uint32_t data, PCB &process);
    void loadProcessData(uint32_t logicalAddress, uint32_t data, PCB &process);

    void setCacheReplacementPolicy(ReplacementPolicy policy);

    // Função auxiliar para o write-back da cache
    void writeToPhysical(uint32_t address, uint32_t data, PCB &process);

    uint32_t readFromPhysical(uint32_t physicalAddress, PCB &process);
    void freeProcessPages(PCB &process);

private:
    std::unique_ptr<MAIN_MEMORY> mainMemory;
    std::unique_ptr<SECONDARY_MEMORY> secondaryMemory;
    std::unique_ptr<Cache> L1_cache; // Adiciona a Cache L1

    size_t mainMemoryLimit;
    mutable std::recursive_mutex memoryMutex;

    uint32_t translateLogicalToPhysical(uint32_t logicalAddress, PCB &process);
    int allocateFreeFrame();
};

#endif // MEMORY_MANAGER_HPP
