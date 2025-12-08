#include "MemoryManager.hpp"

#include <iostream>

MemoryManager::MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheNumLines, size_t cacheLineSizeBytes, size_t pageSize, PolicyType framePolicy)
{
    this->pageSize = pageSize;
    this->totalFrames = mainMemorySize / pageSize;
    this->framesBitmap.resize(totalFrames, false);
    mainMemory = std::make_unique<MAIN_MEMORY>(mainMemorySize);
    secondaryMemory = std::make_unique<SECONDARY_MEMORY>(secondaryMemorySize);
    // Cria cache com política FIFO padrão
    // cacheLineSizeBytes is in bytes, but Cache expects wordsPerLine
    L1_cache = std::make_unique<Cache>(cacheNumLines, cacheLineSizeBytes / sizeof(uint32_t), PolicyType::FIFO);

    mainMemoryLimit = mainMemorySize;

     // Frame Table inicial
    frameTable.resize(totalFrames);

    // Política de substituição configurada via JSON
    currentFramePolicy = framePolicy;

    // Inicializa frames livres na memória secundária
    this->totalSwapFrames = secondaryMemorySize / pageSize;
    for (uint32_t i = 0; i < totalSwapFrames; ++i) {
        freeSwapFrames.push(i);
    }
}

uint32_t MemoryManager::read(uint32_t logicalAddress, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_reads.fetch_add(1);

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

    // std::cout << "[DEBUG] LoadProcessData: PID " << process.pid << " LogAddr " << logicalAddress 
    //           << " PhysAddr " << physicalAddress << " Data " << data << std::endl;

    mainMemory->WriteMem(physicalAddress, data);

    process.mem_writes.fetch_add(1);
    process.primary_mem_accesses.fetch_add(1);
    process.mem_accesses_total.fetch_add(1);
    process.memory_cycles.fetch_add(process.memWeights.primary);
}

void MemoryManager::write(uint32_t logicalAddress, uint32_t data, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    process.mem_accesses_total.fetch_add(1);
    process.mem_writes.fetch_add(1);

    uint32_t physicalAddress = translateLogicalToPhysical(logicalAddress, process);

    L1_cache->write(physicalAddress, data, this, process);

    // std::cout << "Escrevendo na memória através da cache\n";
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
            
            frameTable[i].valid = true;
            frameTable[i].dirty = false;
            frameTable[i].ownerPID = -1;
            frameTable[i].pageNumber = 0;

            // Inserção na política correta
            if (currentFramePolicy == PolicyType::FIFO)
            {
                frameFIFO.push(i);
            }
            else if (currentFramePolicy == PolicyType::LRU)
            {
                frameLRU.push_front(i);
                frameLruPos[i] = frameLRU.begin();
            }

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
            freeFrame = swapOutPage();
        }

        swapInPage(pageNumber, process, freeFrame);

        PageTableEntry entry;
        entry.frameNumber = freeFrame;
        entry.valid = true;
        entry.dirty = false;

        process.pageTable[pageNumber] = entry;

        process.secondary_mem_accesses.fetch_add(1);
        process.mem_accesses_total.fetch_add(1);
        process.mem_writes.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }

    uint32_t physicalFrame = process.pageTable[pageNumber].frameNumber;
    uint32_t physicalAddress = (physicalFrame * this->pageSize) + offset;

    if (physicalAddress >= mainMemoryLimit)
    {
        throw std::runtime_error("Segmentation Fault: Endereço físico calculado fora dos limites da RAM");
    }

    // Atualiza LRU se habilitado
    if (currentFramePolicy == PolicyType::LRU)
    {
        size_t frame = physicalFrame;

        auto it = frameLruPos.find(frame);
        if (it != frameLruPos.end())
            frameLRU.erase(it->second);

        frameLRU.push_front(frame);
        frameLruPos[frame] = frameLRU.begin();
    }

    return physicalAddress;
}

void MemoryManager::setCacheReplacementPolicy(PolicyType policy)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    L1_cache->setReplacementPolicy(policy);
}

// Função chamada pela cache para write-back, ou seja, escrita na memória física diretamente
void MemoryManager::writeToPhysical(uint32_t physicalAddress, uint32_t data, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    if (physicalAddress < mainMemoryLimit)
    {
        mainMemory->WriteMem(physicalAddress, data);
        process.primary_mem_accesses.fetch_add(1);
        process.mem_accesses_total.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    }
    else
    {
        uint32_t secondaryAddress = physicalAddress - mainMemoryLimit;
        secondaryMemory->WriteMem(secondaryAddress, data);
        process.secondary_mem_accesses.fetch_add(1);
        process.mem_accesses_total.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }
}

// Função chamada pela cache para read, ou seja, leitura na memória física diretamente
uint32_t MemoryManager::readFromPhysical(uint32_t physicalAddress, PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    uint32_t data = MEMORY_ACCESS_ERROR;

    if (physicalAddress < mainMemoryLimit)
    {
        data = mainMemory->ReadMem(physicalAddress);
        process.primary_mem_accesses.fetch_add(1);
        process.mem_reads.fetch_add(1);
        process.mem_accesses_total.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.primary);
    }
    else
    {
        uint32_t secondaryAddress = physicalAddress - mainMemoryLimit;
        data = secondaryMemory->ReadMem(secondaryAddress);
        process.secondary_mem_accesses.fetch_add(1);
        process.mem_reads.fetch_add(1);
        process.mem_accesses_total.fetch_add(1);
        process.memory_cycles.fetch_add(process.memWeights.secondary);
    }

    return data;
}

void MemoryManager::freeProcessPages(PCB &process)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    for (auto &[page, entry] : process.pageTable)
    {
        // Sempre remover do swap, se existir
        uint64_t swapID = ((uint64_t)process.pid << 32) | page;
        if (swapMap.count(swapID)) {
            freeSwapFrames.push(swapMap[swapID]);
            swapMap.erase(swapID);
            // std::cout << "[DEBUG] Swap Free (ProcessEnd): PID " << process.pid << " Page " << page 
            //           << " -> SwapFrame " << swapMap[swapID] << ". Remaining: " << freeSwapFrames.size() << std::endl;
        }

        if (!entry.valid)
        {
            continue;
        }

        size_t frame = entry.frameNumber;

        framesBitmap[frame] = false;
        frameTable[frame].valid = false;
        frameTable[frame].ownerPID = -1;

        // Remover da FIFO
        if (currentFramePolicy == PolicyType::FIFO)
        {
            std::queue<size_t> newQueue;
            while (!frameFIFO.empty())
            {
                size_t f = frameFIFO.front();
                frameFIFO.pop();
                if (f != frame)
                    newQueue.push(f);
            }
            frameFIFO = std::move(newQueue);
        }
        else if (currentFramePolicy == PolicyType::LRU)
        {
            auto it = frameLruPos.find(frame);
            if (it != frameLruPos.end())
            {
                frameLRU.erase(it->second);
                frameLruPos.erase(frame);
            }
        }
    }

    process.pageTable.clear();

}

int MemoryManager::chooseVictimFrame()
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    if (currentFramePolicy == PolicyType::FIFO)
    {
        if (frameFIFO.empty()) return -1;
        size_t v = frameFIFO.front();
        frameFIFO.pop();
        return static_cast<int>(v);
    }

    else if (currentFramePolicy == PolicyType::LRU)
    {
        if (frameLRU.empty()) return -1;
        size_t v = frameLRU.back(); // menos usado
        frameLRU.pop_back();
        frameLruPos.erase(v);
        return static_cast<int>(v);
    }

    return -1;
}

int MemoryManager::swapOutPage()
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    int victim = chooseVictimFrame();
    if (victim < 0 || victim >= totalFrames)
        throw std::runtime_error("SwapOut: nenhum frame válido encontrado");

    FrameMetadata &meta = frameTable[victim];

    // 1. Escrever no swap se a página estava válida (e sujeita a ser dirty)
    if (meta.valid)
    {
        if (freeSwapFrames.empty()) {
            throw std::runtime_error("SwapOut: Memória secundária cheia!");
        }

        uint32_t swapFrame = freeSwapFrames.front();
        freeSwapFrames.pop();

        uint64_t swapKey = (uint64_t(meta.ownerPID) << 32) | meta.pageNumber;
        swapMap[swapKey] = swapFrame;
        
        uint32_t baseSwapAddr = swapFrame * pageSize;
        // Copia byte a byte (ou word a word, dependendo da interpretação de pageSize)
        // Mantendo consistência com a implementação anterior que usava loop até pageSize
        for (size_t i = 0; i < pageSize; i++) {
            uint32_t val = mainMemory->ReadMem(victim * pageSize + i);
            secondaryMemory->WriteMem(baseSwapAddr + i, val);
        }
    }

    // 2. INVALIDAR entrada da PAGE TABLE do processo
    PCB *proc = PCB::getProcessByPID(meta.ownerPID);
    if (proc)
    {
        auto it = proc->pageTable.find(meta.pageNumber);
        if (it != proc->pageTable.end())
            it->second.valid = false;
    }

    // Invalidate Cache for this frame
    L1_cache->invalidatePage(victim * pageSize, pageSize, meta.ownerPID, this, proc);

    // 3. Limpar frame
    meta = FrameMetadata();

    return victim;
}

void MemoryManager::swapInPage(uint32_t pageNumber, PCB& process, int freeFrame)
{
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);

    uint64_t swapID = ((uint64_t)process.pid << 32) | pageNumber;

    // size_t wordsPerPage = pageSize / sizeof(uint32_t);
    uint32_t baseAddress = static_cast<uint32_t>(freeFrame * pageSize);

    if (swapMap.count(swapID))
    {
        uint32_t swapFrame = swapMap[swapID];
        uint32_t baseSwapAddr = swapFrame * pageSize;

        // Restaura usando a mesma lógica de loop do swapOut (copia tudo)
        for (size_t i = 0; i < pageSize; ++i) {
            uint32_t val = secondaryMemory->ReadMem(baseSwapAddr + i);
            mainMemory->WriteMem(baseAddress + i, val);
        }

        // Libera o frame de swap
        freeSwapFrames.push(swapFrame);
        swapMap.erase(swapID);
    }
    else
    {
        // Página nova (fill com END_SENTINEL para parar o FetchInstruction)
        for (size_t i = 0; i < pageSize; ++i) {
            mainMemory->WriteMem(baseAddress + i, 0xFC000000);
        }
    }

    FrameMetadata &meta = frameTable[freeFrame];
    meta.ownerPID = process.pid;
    meta.pageNumber = pageNumber;
    meta.valid = true;
    meta.dirty = false;

    if (currentFramePolicy == PolicyType::FIFO)
    {
        frameFIFO.push(static_cast<size_t>(freeFrame));
    }
    else if (currentFramePolicy == PolicyType::LRU)
    {
        frameLRU.push_front(static_cast<size_t>(freeFrame));
        frameLruPos[static_cast<size_t>(freeFrame)] = frameLRU.begin();
    }
}

size_t MemoryManager::getMainMemoryUsage() const {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    size_t used = 0;
    for (const auto& meta : frameTable) {
        if (meta.valid) used++;
    }
    return used;
}

size_t MemoryManager::getSecondaryMemoryUsage() const {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    return swapMap.size();
}

size_t MemoryManager::getCacheUsage() const {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    return L1_cache->getUsage();
}

size_t MemoryManager::getCacheCapacity() const {
    std::lock_guard<std::recursive_mutex> lock(memoryMutex);
    return L1_cache->getCapacity();
}

size_t MemoryManager::getSecondaryMemoryCapacity() const {
    return totalSwapFrames;
}