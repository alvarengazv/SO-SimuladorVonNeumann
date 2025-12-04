#include "MemoryManager.hpp"

#include <iostream>

MemoryManager::MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheNumLines, size_t cacheLineSizeBytes, size_t pageSize)
{
    this->pageSize = pageSize;
    this->totalFrames = mainMemorySize / pageSize;
    this->framesBitmap.resize(totalFrames, false);
    mainMemory = std::make_unique<MAIN_MEMORY>(mainMemorySize);
    secondaryMemory = std::make_unique<SECONDARY_MEMORY>(secondaryMemorySize);
    // Cria cache com política FIFO padrão
    L1_cache = std::make_unique<Cache>(cacheNumLines, cacheLineSizeBytes, ReplacementPolicy::FIFO);

    mainMemoryLimit = mainMemorySize;
}

uint32_t MemoryManager::read(uint32_t logicalAddress, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_reads.fetch_add(1);

    // Translate to physical address BEFORE accessing cache (PIPT)
    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    uint32_t data = L1_cache->read(physicalAddress, this, process);
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);

    return data;
}

void MemoryManager::loadProcessData(uint32_t logicalAddress, uint32_t data, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    mainMemory->WriteMem(physicalAddress, data);
    
    // Invalidate cache line if present (or just rely on PIPT and empty cache at start)
    // Since we are writing directly to memory, we should ensure cache consistency.
    // But loadProcessData is usually called before execution.
    // If we want to be safe, we could write through cache, but that would pollute it.
    // For now, direct write is fine as long as cache is empty or PIPT handles it.

    process.primary_mem_accesses.fetch_add(1);
}

void MemoryManager::write(uint32_t logicalAddress, uint32_t data, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_writes.fetch_add(1);

    // Translate to physical address BEFORE accessing cache (PIPT)
    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    L1_cache->write(physicalAddress, data, this, process);
    std::cout << "Escrevendo na memória através da cache\n";
    process.cache_mem_accesses.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.cache);
}

int MemoryManager::allocateFreeFrame()
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    for (size_t i = 0; i < framesBitmap.size(); ++i)
    {
        if (!framesBitmap[i])
        {
            framesBitmap[i] = true;
            return static_cast<int>(i);
        }
    }
    return -1;
}

uint32_t MemoryManager::translateLogicalToPhysical(uint32_t logicalAddress, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    uint32_t pageNumber = logicalAddress / this->pageSize;
    uint32_t offset = logicalAddress % this->pageSize;

    auto it = process.pageTable.find(pageNumber);

    bool pageFault = (it == process.pageTable.end()) || (!it->second.valid);

    if (pageFault)
    {
        int freeFrame = allocateFreeFrame();

        if (freeFrame == -1)
        {
            throw std::runtime_error("Out of Memory - Swap not implemented");

            // implementar swap aqui
        }

        PageTableEntry newEntry;
        newEntry.frameNumber = static_cast<uint32_t>(freeFrame);
        newEntry.valid = true;
        newEntry.dirty = false;

        process.pageTable[pageNumber] = newEntry;

        // Zero-fill the newly allocated frame to ensure deterministic behavior
        uint32_t frameStartAddr = newEntry.frameNumber * this->pageSize;
        for (uint32_t i = 0; i < this->pageSize; ++i) {
            mainMemory->WriteMem(frameStartAddr + i, 0);
        }

        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }

    uint32_t physicalFrame = process.pageTable[pageNumber].frameNumber;
    uint32_t physicalAddress = (physicalFrame * this->pageSize) + offset;

    if (physicalAddress >= mainMemoryLimit)
    {
        throw std::runtime_error("Segmentation Fault: Endereço físico calculado fora dos limites da RAM");
    }

    return physicalAddress;
}

void MemoryManager::setCacheReplacementPolicy(ReplacementPolicy policy)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    L1_cache->setReplacementPolicy(policy);
}

void MemoryManager::invalidateCache()
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    if (L1_cache)
    {
        L1_cache->invalidate();
    }
}

// Função chamada pela cache para write-back, ou seja, escrita na memória física diretamente
void MemoryManager::writeToPhysical(uint32_t physicalAddress, uint32_t data, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    if (physicalAddress < mainMemoryLimit)
    {
        mainMemory->WriteMem(physicalAddress, data);
        process.primary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    }
    else
    {
        uint32_t secondaryAddress = physicalAddress - mainMemoryLimit;
        secondaryMemory->WriteMem(secondaryAddress, data);
        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }
}

// Função chamada pela cache para read, ou seja, leitura na memória física diretamente
uint32_t MemoryManager::readFromPhysical(uint32_t logicalAddress, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);
    uint32_t data = MEMORY_ACCESS_ERROR;

    if (physicalAddress < mainMemoryLimit)
    {
        data = mainMemory->ReadMem(physicalAddress);
        process.primary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    }
    else
    {
        uint32_t secondaryAddress = physicalAddress - mainMemoryLimit;
        data = secondaryMemory->ReadMem(secondaryAddress);
        process.secondary_mem_accesses.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }

    return data;
}

uint32_t MemoryManager::readRaw(uint32_t physicalAddress)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    if (physicalAddress < mainMemoryLimit)
    {
        return mainMemory->ReadMem(physicalAddress);
    }
    else
    {
        return secondaryMemory->ReadMem(physicalAddress - mainMemoryLimit);
    }
}

void MemoryManager::writeRaw(uint32_t physicalAddress, uint32_t data)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    if (physicalAddress < mainMemoryLimit)
    {
        mainMemory->WriteMem(physicalAddress, data);
    }
    else
    {
        secondaryMemory->WriteMem(physicalAddress - mainMemoryLimit, data);
    }
}

// Função para liberar as páginas alocadas por um processo
void MemoryManager::freeProcessPages(PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    for (auto &entry : process.pageTable)
    {
        if (!entry.second.valid)
        {
            continue;
        }

        uint32_t frameNumber = entry.second.frameNumber;

        if (frameNumber < framesBitmap.size())
        {
            framesBitmap[frameNumber] = false;
        }

        entry.second.valid = false;
    }
}
