#ifndef CACHE_HPP
#define CACHE_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <mutex>
#include "../PCB.hpp"

class MemoryManager;

enum class ReplacementPolicy {
    FIFO,
    LRU
};

struct CacheLine {
    size_t blockId;                 // Qual bloco está armazenado aqui
    std::vector<uint32_t> data;     // Dados do bloco
    bool valid;
    bool dirty;

    CacheLine(size_t blockSize = 0)
        : blockId(0), data(blockSize, 0), valid(false), dirty(false) {}
};

class Cache {
private:
    // Vetor de linhas da cache
    std::vector<CacheLine> lines;

    // Mapeamento: blockId -> index da linha
    std::unordered_map<size_t, size_t> blockToLine;

    // Políticas de substituição
    std::queue<size_t> fifoQueue;                        // Para FIFO
    std::list<size_t> lruOrder;                          // Para LRU
    std::unordered_map<size_t, std::list<size_t>::iterator> lruPos;

    ReplacementPolicy currentPolicy;

    size_t capacity;     // quantidade de linhas (N)
    size_t lineSize;     // tamanho de uma linha em bytes (ou palavras)
    int cache_hits;
    int cache_misses;

    mutable std::mutex cacheMutex;

public:
    Cache(size_t numLines, size_t lineSizeBytes);
    ~Cache();

    // Operações principais
    uint32_t read(uint32_t address, MemoryManager* mem, PCB& process);
    void write(uint32_t address, uint32_t data, MemoryManager* mem, PCB& process);

    // Gerenciamento interno
    void loadBlock(size_t blockId, MemoryManager* mem, PCB& process);
    void evictLine(size_t lineIndex, MemoryManager* mem, PCB& process);

    int get_hits();
    int get_misses();

    // Configuração
    void setReplacementPolicy(ReplacementPolicy policy);
    ReplacementPolicy getReplacementPolicy() const;

    // Utilidades
    void invalidate();
};

#endif
