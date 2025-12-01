#include "MemoryManager.hpp"

#include <iostream>

MemoryManager::MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheCapacity) {
    mainMemory = std::make_unique<MAIN_MEMORY>(mainMemorySize);
    secondaryMemory = std::make_unique<SECONDARY_MEMORY>(secondaryMemorySize);
    L1_cache = std::make_unique<Cache>(cacheCapacity);
    mainMemoryLimit = mainMemorySize;
}

uint32_t MemoryManager::read(uint32_t address, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_reads.fetch_add(1);

    // 1. Tenta ler da Cache
    size_t cache_data = L1_cache->get(address);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);

    if (cache_data != CACHE_MISS) {
        contabiliza_cache(process, true);  // HIT
        return cache_data;
    }

    // 2. Cache Miss: busca na memória correta
    contabiliza_cache(process, false); // MISS

    uint32_t data_from_mem;
    if (address < mainMemoryLimit) {
        process.primary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
        data_from_mem = mainMemory->ReadMem(address);
    } else {
        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
        uint32_t secondaryAddress = address - mainMemoryLimit;
        data_from_mem = secondaryMemory->ReadMem(secondaryAddress);
    }

    // 3. Após a busca, armazena o dado na cache
    L1_cache->put(address, data_from_mem, this, process);

    return data_from_mem;
}

void MemoryManager::write(uint32_t address, uint32_t data, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_writes.fetch_add(1);

    size_t cache_data = L1_cache->get(address);

    if (cache_data == CACHE_MISS) {
        contabiliza_cache(process, false); // MISS
        L1_cache->put(address, data, this, process);
    } else {
        contabiliza_cache(process, true);  // HIT
    }

    // Agora que o dado está na cache, atualiza e marca como "dirty"
    L1_cache->update(address, data);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);
}

void MemoryManager::setCacheReplacementPolicy(int policyCode) {
    if (!L1_cache) {
        return;
    }

    ReplacementPolicy selected;
    switch (policyCode) {
    case 0:
        selected = ReplacementPolicy::FIFO;
        break;
    case 1:
        selected = ReplacementPolicy::LRU;
        break;
    default:
        std::cerr << "[Cache] Política desconhecida (" << policyCode
                  << "). Mantendo padrão FIFO.\n";
        selected = ReplacementPolicy::FIFO;
        break;
    }

    L1_cache->setReplacementPolicy(selected);
}

// Função chamada pela cache para escrever dados "sujos" de volta na memória
void MemoryManager::writeToFile(uint32_t address, uint32_t data, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    if (address < mainMemoryLimit) {
        mainMemory->WriteMem(address, data);
        process.primary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    } else {
        uint32_t secondaryAddress = address - mainMemoryLimit;
        secondaryMemory->WriteMem(secondaryAddress, data);
        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }
}