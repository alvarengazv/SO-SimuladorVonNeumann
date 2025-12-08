#include "replacementPolicy.hpp"
#include <limits>

ReplacementPolicy::ReplacementPolicy() {}

ReplacementPolicy::~ReplacementPolicy() {}

size_t ReplacementPolicy::getAddressToReplace(std::queue<size_t>& fifoQueue) {
    if (type != PolicyType::FIFO)
        return std::numeric_limits<size_t>::max(); // proteção opcional

    if (fifoQueue.empty()) {
        return std::numeric_limits<size_t>::max();
    }

    const size_t addressToRemove = fifoQueue.front();
    fifoQueue.pop();
    return addressToRemove;
}

size_t ReplacementPolicy::getAddressToReplace(std::list<size_t>& usageOrder) {
    if (type != PolicyType::LRU)
        return std::numeric_limits<size_t>::max(); // proteção opcional

    if (usageOrder.empty()) {
        return std::numeric_limits<size_t>::max();
    }

    const size_t addressToRemove = usageOrder.back();
    usageOrder.pop_back();
    return addressToRemove;
}
