#ifndef CPU_CORE_HPP
#define CPU_CORE_HPP

#include <bits/stdc++.h>

#include "../MemoryManager.hpp"
#include "../PCB.hpp"
#include "../IO/IOManager.hpp"

class CPUCore {
    private:
        MemoryManager &memManager;
        PCB &processRunning;
        std::vector<std::unique_ptr<IORequest>> *ioRequests;
        bool printLock;

    public:
        void executeProcess(MemoryManager &memManager, PCB &process, std::vector<std::unique_ptr<IORequest>> *ioRequests, bool printLock);
};

#endif