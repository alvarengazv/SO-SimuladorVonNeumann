#ifndef PROCESS_SCALER_HPP
#define PROCESS_SCALER_HPP
 
#include "../cpu/PCB.hpp"
#include "SCALER.hpp"

#include <cstdint>
#include <cstddef> 
#include <unordered_map>
#include <vector>
#include <queue> 
#include <iostream>
#include <random>
#include <chrono>
using namespace std;
 
   
class ProcessScaler {
private: 
    Scaler scalerEnum;
    vector<PCB*> process;
public:
    ProcessScaler(Scaler scaler,vector<PCB *> process); 

    PCB* scaler(vector<PCB*> process);
    PCB* first_come_first_served(vector<PCB*> process);
    PCB* shortest_job_first(vector<PCB*> process);
    PCB* shortest_remainign_time_first(vector<PCB*> process);
    PCB* round_robin(vector<PCB*> process);
    PCB* priority(vector<PCB*> process);
    void setQuantum();
    void setTimeStamp();
    void setPriority();
};

#endif