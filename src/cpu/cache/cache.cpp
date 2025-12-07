#include "cache.hpp"

Cache::Cache(size_t numLines, size_t wordsPerLine, PolicyType policy)
        : capacity(numLines),
        wordsPerLine(wordsPerLine),
        cache_hits(0),
        cache_misses(0),
        currentPolicy(policy) {
   
    // Inicializa linhas da cache
    lines.reserve(capacity);
    for (size_t i = 0; i < capacity; ++i) {
        lines.emplace_back(wordsPerLine);
    }
}

Cache::~Cache() {
   std::lock_guard<std::recursive_mutex> lock(cacheMutex);

    // Limpa estruturas necessárias
    lruPos.clear();
    lruOrder.clear();
    fifoQueue = std::queue<size_t>();
    blockTagToLine.clear();
    lines.clear();
}

// Decodifica endereço em tag e offset, retornando struct AddressDecoded
AddressDecoded Cache::decodeAddress(uint32_t address, int pid) const {
    size_t blockSizeBytes = wordsPerLine * sizeof(uint32_t);

    AddressDecoded info;
    // Include PID in tag to provide cache isolation between processes
    // Shift PID to upper bits and combine with block address
    size_t blockAddr = address / blockSizeBytes;
    info.tag = (static_cast<size_t>(pid) << 24) | blockAddr;
    info.wordOffset = (address % blockSizeBytes) / sizeof(uint32_t);

    return info;
}

// Leitura da cache
uint32_t Cache::read(uint32_t address, MemoryManager* mem, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(cacheMutex);

    AddressDecoded info = decodeAddress(address, process.pid);

    auto it = blockTagToLine.find(info.tag);
    if (it != blockTagToLine.end()) {
        // HIT
        cache_hits++;
        contabiliza_cache(process, true, "read");
        size_t lineIndex = it->second;

        updateReplacementPolicy(lineIndex);
        return lines[lineIndex].data[info.wordOffset];
    } else {
        // MISS
        cache_misses++;
        contabiliza_cache(process, false, "read");

        // Carrega o bloco da memória principal para a cache
        size_t lineIndex = getLineToEvict();
        evictLine(lineIndex, mem, process);
        loadBlock(info.tag, lineIndex, mem, process);

        return lines[lineIndex].data[info.wordOffset];
    }
}

// Escrita da cache
void Cache::write(uint32_t address, uint32_t data, MemoryManager* mem, PCB& process) {
    std::lock_guard<std::recursive_mutex> lock(cacheMutex);

    AddressDecoded info = decodeAddress(address, process.pid);

    auto it = blockTagToLine.find(info.tag);
    size_t lineIndex;

    if (it != blockTagToLine.end()) {
        // HIT
        cache_hits++;
        contabiliza_cache(process, true, "write");
        lineIndex = it->second;

        updateReplacementPolicy(lineIndex);
    } else {
        // MISS → write-allocate
        cache_misses++;
        contabiliza_cache(process, false, "write");

        // Carrega o bloco da memória principal para a cache
        lineIndex = getLineToEvict();
        evictLine(lineIndex, mem, process);
        loadBlock(info.tag, lineIndex, mem, process);
    }

    lines[lineIndex].data[info.wordOffset] = data;

    lines[lineIndex].dirty = true;
}

// Carrega um bloco da memória principal para a cache
void Cache::loadBlock(size_t blockTag, size_t lineIndex, MemoryManager* mem, PCB& process) {
    CacheLine& line = lines[lineIndex];

    size_t blockSizeBytes = wordsPerLine * sizeof(uint32_t);
    // Extract the address part from the tag (lower 24 bits) since upper bits contain PID
    uint32_t blockAddr = blockTag & 0xFFFFFF;
    uint32_t baseAddress = blockAddr * blockSizeBytes;

    for (size_t i = 0; i < wordsPerLine; i++) {
        uint32_t wordAddress = baseAddress + (i * sizeof(uint32_t));
        line.data[i] = mem->readFromPhysical(wordAddress, process);
    }

    line.tag = blockTag;
    line.valid = true;
    line.dirty = false;

    // Atualiza mapeamento bloco → linha
    blockTagToLine[blockTag] = lineIndex;

    // Atualiza política de substituição
    updateReplacementPolicy(lineIndex);

    if (currentPolicy == PolicyType::FIFO) {
        fifoQueue.push(lineIndex);
    }
}

// Força escrita do bloco se dirty
void Cache::evictLine(size_t lineIndex, MemoryManager* mem, PCB& process) {
    CacheLine& line = lines[lineIndex];

    if (line.valid && line.dirty) {
        size_t blockSizeBytes = wordsPerLine * sizeof(uint32_t);
        // Extract the address part from the tag (lower 24 bits) since upper bits contain PID
        uint32_t blockAddr = line.tag & 0xFFFFFF;
        uint32_t baseAddress = blockAddr * blockSizeBytes;

        for (size_t i = 0; i < wordsPerLine; ++i) {
            uint32_t wordAddress = baseAddress + (i * sizeof(uint32_t));
            mem->writeToPhysical(wordAddress, line.data[i], process);
        }
    }

    if (line.valid) {
        blockTagToLine.erase(line.tag);
    }

    line.valid = false;
    line.dirty = false;
    line.tag = 0;
}

// Invalida toda a cache
void Cache::invalidate() {
    std::lock_guard<std::recursive_mutex> lock(cacheMutex);

    for (size_t i = 0; i < capacity; ++i) {
        CacheLine& line = lines[i];

        line.valid = false;
        line.dirty = false;
        line.tag = 0;
    }

    blockTagToLine.clear();

    // Limpa estruturas de política de substituição
    fifoQueue = std::queue<size_t>();
    lruOrder.clear();
    lruPos.clear();
}

// Getters para hits e misses
int Cache::get_misses() {
    // Retorna o número de cache misses
    return cache_misses;
}

int Cache::get_hits() {
    // Retorna o número de cache hits
    return cache_hits;
}

// Obtém o índice da linha a ser evictada conforme a política atual
size_t Cache::getLineToEvict() {

    for (size_t i = 0; i < capacity; ++i) {
        if (!lines[i].valid) {
            return i;
        }
    }

    size_t victimIndex;

    if (currentPolicy == PolicyType::FIFO) {
        // A CachePolicy remove da fila e retorna o índice
        // victimIndex = policyHandler.getAddressToReplace(fifoQueue);
        
        // Verificação de segurança (caso a fila estivesse vazia, retornaria max)
        if (fifoQueue.empty()) {
            throw std::runtime_error("Erro: Tentativa de evict em fila FIFO vazia.");
        }
        victimIndex = fifoQueue.front();
        fifoQueue.pop();

    } else { // LRU
        // A CachePolicy remove do final da lista (back) e retorna o índice
        // victimIndex = policyHandler.getAddressToReplace(lruOrder);

        if (lruOrder.empty()) {
             throw std::runtime_error("Erro: Tentativa de evict em lista LRU vazia.");
        }
        victimIndex = lruOrder.back();
        lruOrder.pop_back();
        
        // Removemos do mapa auxiliar de iteradores, pois ele saiu da lista
        lruPos.erase(victimIndex);
    }

    return victimIndex;
}

// Atualiza estruturas da política de substituição após acesso
void Cache::updateReplacementPolicy(size_t lineIndex) {
    if (currentPolicy == PolicyType::FIFO) {
        return;  // FIFO não requer atualização no acesso
    } else if (currentPolicy == PolicyType::LRU) {
        // Se a linha já está na lista, removemos sua posição antiga
        auto it = lruPos.find(lineIndex);
        if (it != lruPos.end()) {
            lruOrder.erase(it->second);
        }

        // Adicionamos a linha no início da lista (mais recentemente usada)
        lruOrder.push_front(lineIndex);
        lruPos[lineIndex] = lruOrder.begin();
    }
}

// Set e get para a política de substituição
void Cache::setReplacementPolicy(PolicyType policy) {
    if (currentPolicy == policy) {
        return;
    }

    currentPolicy = policy;

    // Limpa e reinicia as estruturas auxiliares para evitar rastros da política anterior
    invalidate();
}

PolicyType Cache::getReplacementPolicy() const {
    return currentPolicy;
}

size_t Cache::getUsage() const {
    std::lock_guard<std::recursive_mutex> lock(cacheMutex);
    size_t used = 0;
    for (const auto& line : lines) {
        if (line.valid) {
            used++;
        }
    }
    return used;
}

size_t Cache::getCapacity() const {
    return capacity;
}

void Cache::invalidatePage(uint32_t physicalAddressStart, size_t size, int pid, MemoryManager* mem, PCB* process) {
    std::lock_guard<std::recursive_mutex> lock(cacheMutex);
    
    size_t blockSizeBytes = wordsPerLine * sizeof(uint32_t);
    
    // Iterate through all blocks that could be in this page
    for (uint32_t addr = physicalAddressStart; addr < physicalAddressStart + size; addr += blockSizeBytes) {
        AddressDecoded info = decodeAddress(addr, pid);
        
        auto it = blockTagToLine.find(info.tag);
        if (it != blockTagToLine.end()) {
            size_t lineIndex = it->second;
            
            // Write back if dirty
            if (lines[lineIndex].dirty && lines[lineIndex].valid && process != nullptr) {
                evictLine(lineIndex, mem, *process);
                // evictLine invalidates the line and removes from map.
                continue;
            }

            // Invalidate line (evictLine might have already done this, but let's be sure)
            lines[lineIndex].valid = false;
            lines[lineIndex].dirty = false;
            lines[lineIndex].tag = 0;
            
            // Remove from map
            blockTagToLine.erase(it);
        }
    }
}
