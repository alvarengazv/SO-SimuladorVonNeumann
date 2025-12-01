#include "cache.hpp"
#include "cachePolicy.hpp"
#include "../MemoryManager.hpp" // Necessário para a lógica de write-back

#include <limits>

Cache::Cache()
    : currentPolicy(ReplacementPolicy::FIFO) {
    this->capacity = CACHE_CAPACITY;
    this->cacheMap.reserve(CACHE_CAPACITY);
    this->lruPositions.reserve(CACHE_CAPACITY);
    this->cache_misses = 0;
    this->cache_hits = 0;
}

Cache::~Cache() {
    this->cacheMap.clear();
    this->lruOrder.clear();
    this->lruPositions.clear();
}

size_t Cache::get(size_t address) {
    auto entryIt = cacheMap.find(address);
    if (entryIt != cacheMap.end() && entryIt->second.isValid) {
        cache_hits++;

        if (currentPolicy == ReplacementPolicy::LRU) {
            auto posIt = lruPositions.find(address);
            if (posIt != lruPositions.end()) {
                lruOrder.splice(lruOrder.begin(), lruOrder, posIt->second);
                posIt->second = lruOrder.begin();
            }
        }

        return entryIt->second.data; // Cache hit
    }

    cache_misses++;
    return CACHE_MISS; // Cache miss
}

void Cache::put(size_t address, size_t data, MemoryManager* memManager) {
    auto existingIt = cacheMap.find(address);
    if (existingIt != cacheMap.end()) {
        existingIt->second.data = data;
        existingIt->second.isValid = true;
        existingIt->second.isDirty = false;

        if (currentPolicy == ReplacementPolicy::LRU) {
            auto posIt = lruPositions.find(address);
            if (posIt != lruPositions.end()) {
                lruOrder.splice(lruOrder.begin(), lruOrder, posIt->second);
                posIt->second = lruOrder.begin();
            }
        }
        return;
    }

    // Se a cache está cheia, precisamos remover um item
    if (cacheMap.size() >= capacity) {
        CachePolicy cachepolicy;
        size_t addr_to_remove;
        if (currentPolicy == ReplacementPolicy::LRU) {
            addr_to_remove = cachepolicy.getAddressToReplace(lruOrder);
        } else {
            addr_to_remove = cachepolicy.getAddressToReplace(fifoQueue);
        }

        if (addr_to_remove != std::numeric_limits<size_t>::max()) {
            auto removeIt = cacheMap.find(addr_to_remove);
            if (removeIt != cacheMap.end()) {
                CacheEntry& entry_to_remove = removeIt->second;

                if (entry_to_remove.isDirty) {
                    memManager->writeToFile(addr_to_remove, entry_to_remove.data);
                }
                cacheMap.erase(removeIt);
            }

            if (currentPolicy == ReplacementPolicy::LRU) {
                lruPositions.erase(addr_to_remove);
            }
        }
    }

    // Adiciona o novo item na cache
    CacheEntry new_entry;
    new_entry.data = data;
    new_entry.isValid = true;
    new_entry.isDirty = false; // Começa como "limpo"

    cacheMap[address] = new_entry;
    if (currentPolicy == ReplacementPolicy::LRU) {
        lruOrder.push_front(address);
        lruPositions[address] = lruOrder.begin();
    } else {
        fifoQueue.push(address);
    }
}

void Cache::update(size_t address, size_t data) {
    // Se o item não está na cache, primeiro o colocamos lá
    if (cacheMap.find(address) == cacheMap.end()) {
        // Para a simplicidade, assumimos que o `put` deve ser chamado pelo `MemoryManager`
        // em um cache miss de escrita. Aqui, focamos em atualizar.
        // Em um sistema real, aqui ocorreria um "write-allocate".
        // Por ora, vamos apenas atualizar se existir.
        return;
    }
    
    cacheMap[address].data = data;
    cacheMap[address].isDirty = true; // Marca como sujo
    cacheMap[address].isValid = true;

    if (currentPolicy == ReplacementPolicy::LRU) {
        auto posIt = lruPositions.find(address);
        if (posIt != lruPositions.end()) {
            lruOrder.splice(lruOrder.begin(), lruOrder, posIt->second);
            posIt->second = lruOrder.begin();
        }
    }
}

void Cache::invalidate() {
    for (auto &c : cacheMap) {
        c.second.isValid = false;
    }
    // Limpar as estruturas auxiliares também, pois a cache foi invalidada
    std::queue<size_t> empty;
    fifoQueue.swap(empty);
    lruOrder.clear();
    lruPositions.clear();
}

std::vector<std::pair<size_t, size_t>> Cache::dirtyData() {
    std::vector<std::pair<size_t, size_t>> dirty_data;
    for (const auto &c : cacheMap) {
        if (c.second.isDirty) {
            dirty_data.emplace_back(c.first, c.second.data);
        }
    }
    return dirty_data;
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
    std::queue<size_t> empty;
    fifoQueue.swap(empty);
    lruOrder.clear();
    lruPositions.clear();

    if (currentPolicy == ReplacementPolicy::LRU) {
        lruPositions.reserve(cacheMap.size());
    }

    for (const auto& entry : cacheMap) {
        if (!entry.second.isValid) {
            continue;
        }

        if (currentPolicy == ReplacementPolicy::FIFO) {
            fifoQueue.push(entry.first);
        } else {
            lruOrder.push_front(entry.first);
            lruPositions[entry.first] = lruOrder.begin();
        }
    }
}

ReplacementPolicy Cache::getReplacementPolicy() const {
    return currentPolicy;
}