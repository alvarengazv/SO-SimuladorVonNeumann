#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <memory>
#include <stdexcept>
#include <mutex>
#include "../memory/MAIN_MEMORY.hpp"
#include "../memory/SECONDARY_MEMORY.hpp"
#include "cache/cache.hpp" // Incluir a cache
#include "PCB.hpp" // Incluir o PCB para as métricas

const size_t MAIN_MEMORY_SIZE = 1024;

class MemoryManager {
public:
    MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheCapacity);

    // Métodos unificados agora recebem o PCB para as métricas
    uint32_t read(uint32_t address, PCB& process);
    void write(uint32_t address, uint32_t data, PCB& process);

    void setCacheReplacementPolicy(int policyCode);
    
    // Função auxiliar para o write-back da cache
    void writeToFile(uint32_t address, uint32_t data, PCB& process);

private:
    std::unique_ptr<MAIN_MEMORY> mainMemory;
    std::unique_ptr<SECONDARY_MEMORY> secondaryMemory;
    std::unique_ptr<Cache> L1_cache; // Adiciona a Cache L1

    size_t mainMemoryLimit;
    mutable std::recursive_mutex memoryMutex;
};

#endif // MEMORY_MANAGER_HPP