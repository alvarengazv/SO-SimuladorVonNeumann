#include "MemoryManager.hpp"

#include <iostream>

MemoryManager::MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheCapacity) {
    mainMemory = std::make_unique<MAIN_MEMORY>(mainMemorySize);
    secondaryMemory = std::make_unique<SECONDARY_MEMORY>(secondaryMemorySize);
    L1_cache = std::make_unique<Cache>(cacheNumLines, cacheLineSizeBytes);

    mainMemoryLimit = mainMemorySize;

    pageSizeBytes = SystemConfig::get().main_memory.page_size;
    pageSizeWords = pageSizeBytes / sizeof(uint32_t);  
}

uint32_t MemoryManager::read(uint32_t logicalAddress, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_reads.fetch_add(1);

    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    // 2. Tenta ler da cache L1 (por blocos)
    uint32_t data = L1_cache->read(physicalAddress, this, process);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);

    return data;
}

void MemoryManager::write(uint32_t logicalAddress, uint32_t data, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_writes.fetch_add(1);

    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    // 2. Escreve na cache L1 (write-back)
    L1_cache->write(physicalAddress, data, this, process);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);
}

uint32_t MemoryManager::translateLogicalToPhysical(uint32_t logicalAddress, PCB& process) {
    size_t pageId = logicalAddress / pageSizeBytes;
    size_t offset = logicalAddress % pageSizeBytes;

    size_t frame;
    if (!mainMemory->isPageLoaded(pageId)) {
        // Page fault: carrega página da memória secundária
        auto pageData = secondaryMemory->loadPage(pageId);
        frame = mainMemory->loadPageIntoFrame(pageId, pageData);
    } else {
        frame = mainMemory->getFrameForPage(pageId);
    }

    return static_cast<uint32_t>(frame * pageSizeBytes + offset);
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
