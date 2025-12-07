#include "PCB.hpp"

std::unordered_map<int, PCB*> PCB::processTable;
std::mutex PCB::processTableMutex;

void PCB::registerProcess(PCB* proc) {
    if (!proc) return;
    std::lock_guard<std::mutex> lock(processTableMutex);
    processTable[proc->pid] = proc;
}

void PCB::unregisterProcess(int pid) {
    std::lock_guard<std::mutex> lock(processTableMutex);
    processTable.erase(pid);
}

PCB* PCB::getProcessByPID(int pid) {
    std::lock_guard<std::mutex> lock(processTableMutex);
    auto it = processTable.find(pid);
    return (it != processTable.end()) ? it->second : nullptr;
}

void PCB::appendProgramOutput(const std::string &line) {
    std::lock_guard<std::mutex> lock(outputMutex);
    programOutput.push_back(line);
}