#ifndef REPLACEMENT_POLICY_HPP
#define REPLACEMENT_POLICY_HPP

#include <cstddef>
#include <queue>
#include <list>

// Tipos de política suportados
enum class PolicyType {
    FIFO,
    LRU
};

class ReplacementPolicy {
public:
    ReplacementPolicy();
    ~ReplacementPolicy();

    size_t getAddressToReplace(std::queue<size_t>& fifoQueue);
    size_t getAddressToReplace(std::list<size_t>& usageOrder);

    PolicyType getType() const { return type; }

private:
    PolicyType type;
};

#endif
