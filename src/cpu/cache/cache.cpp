#include "cache.hpp"
#include "cachePolicy.hpp"
#include "../MemoryManager.hpp" // Necessário para a lógica de write-back

#include <limits>

Cache::Cache(size_t numLines, size_t lineSizeBytes)
    : numLines(numLines),
      lineSizeBytes(lineSizeBytes),
      hits(0),
      misses(0),
      currentPolicy(ReplacementPolicy::FIFO) {

    wordsPerBlock = lineSizeBytes / sizeof(unint32_t);
    lines.resize(numLines);

    // inicializar todas as linhas como inválidas
    for (auto& line : lines) {
        line.valid = false;
        line.dirty = false;
        line.tag = 0;
        line.blockData.resize(wordsPerBlock, 0);
    }
}

Cache::~Cache() {
    // Limpa linhas e estruturas auxiliares
    lines.clear();
    fifoQueue = std::queue<size_t>();
    lruList.clear();
    lruPositions.clear();
}


// Divide endereço em tag | index | offset
void Cache::parseAddress(uint32_t address, uint32_t &tag, size_t &index, size_t &offset) const {
    size_t blockNumber = address / lineSizeBytes;
    index = blockNumber % numLines;
    tag = blockNumber / numLines;
    offset = (address % lineSizeBytes) / sizeof(uint32_t);
}

// Leitura da cache
uint32_t Cache::read(uint32_t address, MemoryManager* mem, PCB& process) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    uint32_t tag;
    size_t index, offset;
    parseAddress(address, tag, index, offset);

    CacheLine &line = lines[index];
    if (line.valid && line.tag == tag) {
        hits++;

        // Atualiza LRU se necessário
        if (currentPolicy == ReplacementPolicy::LRU) {
            lruList.erase(lruPositions[index]);
            lruList.push_front(index);
            lruPositions[index] = lruList.begin();
        }

        return line.blockData[offset];
    }

    // Cache miss
    misses++;
    loadBlock(address / lineSizeBytes, mem, process);
    return lines[index].blockData[offset];
}

// Escrita da cache
void Cache::write(uint32_t address, uint32_t data, MemoryManager* mem, PCB& process) {
    std::lock_guard<std::mutex> lock(cacheMutex);

    uint32_t tag;
    size_t index, offset;
    parseAddress(address, tag, index, offset);

    CacheLine &line = lines[index];
    if (!line.valid || line.tag != tag) {
        // Write-allocate: carrega bloco antes de escrever
        loadBlock(address / lineSizeBytes, mem, process);
        line = lines[index];
    }

    line.blockData[offset] = data;
    line.dirty = true;

    // Atualiza LRU se necessário
    if (currentPolicy == ReplacementPolicy::LRU) {
        lruList.erase(lruPositions[index]);
        lruList.push_front(index);
        lruPositions[index] = lruList.begin();
    }
}

// Carrega um bloco da memória principal para a cache
void Cache::loadBlock(size_t blockId, MemoryManager* mem, PCB& process) {
    size_t index = blockId % numLines;
    CacheLine &line = lines[index];

    // Se linha atual é dirty, escreve de volta
    if (line.valid && line.dirty) {
        for (size_t i = 0; i < wordsPerBlock; ++i) {
            mem->writeToFile((line.tag * numLines + index) * wordsPerBlock + i, line.blockData[i], process);
        }
    }

    // Atualiza a linha com novo bloco
    line.tag = blockId / numLines;
    line.valid = true;
    line.dirty = false;

    // Lê bloco inteiro da memória
    line.blockData.resize(wordsPerBlock);
    for (size_t i = 0; i < wordsPerBlock; ++i) {
        line.blockData[i] = mem->readFromFile(blockId * wordsPerBlock + i, process);
    }

    // Atualiza estruturas de substituição
    if (currentPolicy == ReplacementPolicy::FIFO) {
        // Remove se já estiver na fila (simula reinserção)
        std::queue<size_t> newQueue;
        while (!fifoQueue.empty()) {
            size_t idx = fifoQueue.front(); fifoQueue.pop();
            if (idx != index) newQueue.push(idx);
        }
        fifoQueue = newQueue;
        fifoQueue.push(index);
    } else { // LRU
        lruList.remove(index);
        lruList.push_front(index);
        lruPositions[index] = lruList.begin();
    }
}

// Força escrita do bloco se dirty
void Cache::evictLine(size_t lineIndex, MemoryManager* mem, PCB& process) {
    CacheLine &line = lines[lineIndex];
    if (line.valid && line.dirty) {
        for (size_t i = 0; i < wordsPerBlock; ++i) {
            mem->writeToFile((line.tag * numLines + lineIndex) * wordsPerBlock + i, line.blockData[i], process);
        }
    }
    line.valid = false;
    line.dirty = false;
}

int Cache::get_misses(){
       // Retorna o número de cache misses
    return cache_misses;
}

int Cache::get_hits(){
       // Retorna o número de cache hits
    return cache_hits;
}

void Cache::setReplacementPolicy(ReplacementPolicy policy) {
    if (currentPolicy == policy) {
        return;
    }

    currentPolicy = policy;

    // Limpa e reinicia as estruturas auxiliares para evitar rastros da política anterior
    fifoQueue = std::queue<size_t>();
    lruOrder.clear();
    lruPositions.clear();

    for (size_t i = 0; i < lines.size(); ++i) {
        if (!lines[i].valid) continue;
        if (currentPolicy == ReplacementPolicy::FIFO) {
            fifoQueue.push(i);
        } else {
            lruList.push_front(i);
            lruPositions[i] = lruList.begin();
        }
    }
}

ReplacementPolicy Cache::getReplacementPolicy() const {
    return currentPolicy;
}

void Cache::invalidate() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (auto &line : lines) {
        line.valid = false;
        line.dirty = false;
    }

    // Limpar as estruturas auxiliares também, pois a cache foi invalidada
    fifoQueue = std::queue<size_t>();
    lruOrder.clear();
    lruPositions.clear();
}
