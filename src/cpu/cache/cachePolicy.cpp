#include "cachePolicy.hpp"

#include <limits>

CachePolicy::CachePolicy() {}

CachePolicy::~CachePolicy() {}

size_t CachePolicy::getAddressToReplace(std::queue<size_t>& fifoQueue) {
    if (fifoQueue.empty()) {
        return std::numeric_limits<size_t>::max();
    }

    const size_t addressToRemove = fifoQueue.front();
    fifoQueue.pop();
    return addressToRemove;
}

size_t CachePolicy::getAddressToReplace(std::list<size_t>& usageOrder) {
    if (usageOrder.empty()) {
        return std::numeric_limits<size_t>::max();
    }

    const size_t addressToRemove = usageOrder.back();
    usageOrder.pop_back();
    return addressToRemove;
}