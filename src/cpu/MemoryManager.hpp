#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include <memory>
#include <stdexcept>
#include <mutex>
#include "../memory/MAIN_MEMORY.hpp"
#include "../memory/SECONDARY_MEMORY.hpp"
#include "../memory/replacementPolicy.hpp"
#include "cache/cache.hpp"
#include "PCB.hpp"

// Forward declarations para evitar ciclo de includes
class PCB;
class Cache;

struct FrameMetadata {
    int ownerPID = -1;           // Quem usa esse frame
    uint32_t pageNumber = 0;     // Número da página mapeada
    bool dirty = false;          // Página foi modificada?
    bool valid = false;          // Frame está sendo usado?
};

class MemoryManager
{
public:
    size_t pageSize;
    size_t totalFrames;
    size_t totalSwapFrames;
    std::vector<bool> framesBitmap;

    MemoryManager(size_t mainMemorySize, size_t secondaryMemorySize, size_t cacheNumLines, size_t cacheLineSizeBytes, size_t pageSize, PolicyType framePolicy);

    // Métodos unificados agora recebem o PCB para as métricas
    uint32_t read(uint32_t LogicalAddress, PCB &process);
    void write(uint32_t LogicalAddress, uint32_t data, PCB &process);
    void loadProcessData(uint32_t logicalAddress, uint32_t data, PCB &process);

    void setCacheReplacementPolicy(PolicyType policy);

    // Função auxiliar para o write-back da cache
    void writeToPhysical(uint32_t address, uint32_t data, PCB &process);
    uint32_t readFromPhysical(uint32_t physicalAddress, PCB &process);
    void freeProcessPages(PCB &process);

    int chooseVictimFrame();
    int swapOutPage();
    void swapInPage(uint32_t pageNumber, PCB& process, int freeFrame);

    // Métricas de uso
    size_t getMainMemoryUsage() const;
    size_t getSecondaryMemoryUsage() const;
    size_t getCacheUsage() const;

    size_t getCacheCapacity() const;
    size_t getSecondaryMemoryCapacity() const;

private:
    std::unique_ptr<MAIN_MEMORY> mainMemory;
    std::unique_ptr<SECONDARY_MEMORY> secondaryMemory;
    std::unique_ptr<Cache> L1_cache; // Adiciona a Cache L1

    size_t mainMemoryLimit;
    mutable std::recursive_mutex memoryMutex;

    uint32_t translateLogicalToPhysical(uint32_t logicalAddress, PCB &process);
    int allocateFreeFrame();

    std::vector<FrameMetadata> frameTable;

    // std::unordered_map<uint64_t, SwappedPage> swapSpace;
    std::queue<uint32_t> freeSwapFrames;
    std::unordered_map<uint64_t, uint32_t> swapMap; // (pid << 32 | page) -> swapFrameIndex

    std::queue<size_t> frameFIFO;
    std::list<size_t> frameLRU; 
    std::unordered_map<size_t, std::list<size_t>::iterator> frameLruPos;

    PolicyType currentFramePolicy;
};

#endif // MEMORY_MANAGER_HPP
