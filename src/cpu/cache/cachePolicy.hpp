#ifndef CACHE_POLICY_HPP
#define CACHE_POLICY_HPP

#include <cstddef>
#include <list>
#include <queue>

class CachePolicy {
public:
    CachePolicy();
    ~CachePolicy();

    size_t getAddressToReplace(std::queue<size_t>& fifoQueue);
    size_t getAddressToReplace(std::list<size_t>& usageOrder);
};

#endif