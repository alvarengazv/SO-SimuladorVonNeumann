#include "MemoryManager.hpp"

#include <iostream>

MemoryManager::MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheNumLines, size_t cacheLineSizeBytes) {
    mainMemory = std::make_unique<MAIN_MEMORY>(mainMemorySize);
    secondaryMemory = std::make_unique<SECONDARY_MEMORY>(secondaryMemorySize);
    // Cria cache com política FIFO padrão
    L1_cache = std::make_unique<Cache>(cacheNumLines, cacheLineSizeBytes, ReplacementPolicy::FIFO);

    mainMemoryLimit = mainMemorySize;
}

uint32_t MemoryManager::read(uint32_t logicalAddress, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_reads.fetch_add(1);

    // A cache já trata o mapeamento lógico → físico internamente
    uint32_t data = L1_cache->read(logicalAddress, this, process);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);

    return data;
}

void MemoryManager::write(uint32_t logicalAddress, uint32_t data, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_writes.fetch_add(1);

    // A cache já trata o mapeamento lógico → físico internamente
    L1_cache->write(logicalAddress, data, this, process);
    std::cout << "Escrevendo na memória através da cache\n";
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);
}

uint32_t MemoryManager::translateLogicalToPhysical(uint32_t logicalAddress, PCB& process) {
    // Por enquanto, retorna o endereço lógico como físico (sem MMU)
    // Implementar paging/segmentação conforme necessário
    return logicalAddress;
}

void MemoryManager::setCacheReplacementPolicy(ReplacementPolicy policy) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    L1_cache->setReplacementPolicy(policy);
}

// Função chamada pela cache para write-back
void MemoryManager::writeToFile(uint32_t physicalAddress, uint32_t data, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    if (physicalAddress < mainMemoryLimit) {
        mainMemory->WriteMem(physicalAddress, data);
        process.primary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    } else {
        uint32_t secondaryAddress = physicalAddress - mainMemoryLimit;
        secondaryMemory->WriteMem(secondaryAddress, data);
        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }
}
