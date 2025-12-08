#ifndef CACHE_HPP
#define CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "../MemoryManager.hpp"
#include "../../memory/replacementPolicy.hpp"
#include "../PCB.hpp"

// Forward declarations para evitar ciclo de includes
class MemoryManager;
class PCB;

struct AddressDecoded {
    size_t tag;         // Qual bloco é? (inclui PID para isolamento)
    size_t wordOffset;  // Qual palavra dentro do bloco?
};

struct CacheLine {
    size_t tag;                  // Qual bloco está armazenado aqui
    std::vector<uint32_t> data;  // Dados do bloco
    bool valid;
    bool dirty;

    CacheLine(size_t wordsPerBlock = 0)
        : tag(0), data(wordsPerBlock, 0), valid(false), dirty(false) {}
};

class Cache {
   private:
    // Vetor de linhas da cache
    std::vector<CacheLine> lines;

    // numero de linhas na cache
    const size_t capacity;

    // numero de palavras por linha
    const size_t wordsPerLine;

    // Mapeamento: tag do bloco -> índice da linha na cache
    std::unordered_map<size_t, size_t> blockTagToLine;

    // Políticas de substituição
    std::queue<size_t> fifoQueue;  // Para FIFO
    std::list<size_t> lruOrder;    // Para LRU
    std::unordered_map<size_t, std::list<size_t>::iterator> lruPos;

    PolicyType currentPolicy;
    ReplacementPolicy policyHandler;

    AddressDecoded decodeAddress(uint32_t address, int pid) const;

    int cache_hits;
    int cache_misses;

    mutable std::recursive_mutex cacheMutex;

    size_t getLineToEvict();
    void updateReplacementPolicy(size_t lineIndex);
    void loadBlock(size_t blockTag, size_t lineIndex, MemoryManager* mem, PCB& process);
    void evictLine(size_t lineIndex, MemoryManager* mem, PCB& process);

   public:
    Cache(size_t numLines, size_t wordsPerLine, PolicyType policy);
    ~Cache();

    // Operações principais
    uint32_t read(uint32_t address, MemoryManager* mem, PCB& process);
    void write(uint32_t address, uint32_t data, MemoryManager* mem, PCB& process);

    int get_hits();
    int get_misses();

    // Configuração
    void setReplacementPolicy(PolicyType policy);
    PolicyType getReplacementPolicy() const;

    // Utilidades
    void invalidate();
    void invalidatePage(uint32_t physicalAddressStart, size_t size, int pid, MemoryManager* mem, PCB* process);

    // Métricas de uso
    size_t getUsage() const;
    size_t getCapacity() const;
};

#endif
