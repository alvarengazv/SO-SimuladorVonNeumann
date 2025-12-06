#include "process_scheduler.hpp"
#include "scheduler.hpp"

ProcessScheduler::ProcessScheduler(int schedulerInt, vector<PCB *> process)
{
    this->schedulerInt = schedulerInt;
    this->process = process;
    switch (schedulerInt)
    {

    case 0:
        this->setQuantum();
        break;

    case 1:
        this->setQuantum();
        break;

    case 2:
        this->setQuantum();
        this->setTickets();
        break;

    case 3:
        this->setPriority();
        break;
    default:
        break;
    }
}

void ProcessScheduler::setQuantum()
{
    for (int i = 0; i < this->process.size(); i++)
    {
        int min_val = 5;
        int max_val = 30;
        unsigned seed = 42 + i; // Seed fixa para reprodutibilidade
        mt19937 rng(seed);

        uniform_int_distribution<int> dist(min_val, max_val);

        int random_number = dist(rng);

        this->process.at(i)->quantum = random_number;
    }
}

void ProcessScheduler::setPriority()
{
    for (int i = 0; i < this->process.size(); i++)
    {
        int min_val = 0;
        int max_val = 5;
        unsigned seed = 616 + i;
        mt19937 rng(seed);

        uniform_int_distribution<int> dist(min_val, max_val);

        int random_number = dist(rng);

        this->process.at(i)->priority = random_number;
    }
}

void ProcessScheduler::setTimeStamp()
{
    for (int i = 0; i < this->process.size(); i++)
    {
        this->process.at(i)->timeStamp = 0;
    }
}

void ProcessScheduler::setTickets()
{
    if (this->process.empty()) return;

    int maxInstr = 0;
    for (auto *p : this->process) {
        if (p && p->instructions > maxInstr) maxInstr = p->instructions;
    }

    const int minTickets = 1;
    const int maxTickets = 20; 
    
    if (maxInstr <= 0) {
        for (auto *p : this->process) if (p) p->tickets = minTickets;
        return;
    }

    for (auto *p : this->process) {
        if (!p) continue;
        int scaled = static_cast<int>((static_cast<double>(p->instructions) / maxInstr) * maxTickets + 0.5);
        if (scaled < minTickets) scaled = minTickets;
        p->tickets = scaled;
        std::cout << "Tickets atribuido para o processo: " << p->name << " -> " << p->tickets << " (instructions=" << p->instructions << ")\n";
    }
}

PCB *ProcessScheduler::scheduler(vector<PCB *> process)
{

    switch (this->schedulerInt)
    {

    case 0:
        return round_robin(process);
        break;

    case 1:
        return shortest_job_first(process);
        break;

    case 2:
        return lotterySelect(process);
        break;

    case 3:
        return priority(process);
        break;
    default:
        return first_come_first_served(process);
        break;
    }
}

PCB *ProcessScheduler::shortest_job_first(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;

    PCB *selected_process = process[0];

    for (const auto &p : process)
    {
        if (p->instructions < selected_process->instructions)
        {
            selected_process = p;
        }
    }
    return selected_process;
}

PCB *ProcessScheduler::round_robin(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;
    return process.front();
}

PCB *ProcessScheduler::priority(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;

    PCB *selected_process = process[0];

    for (const auto &p : process)
    {

        if (p->priority < selected_process->priority)
        {
            selected_process = p;
        }
    }
    return selected_process;
}

PCB *ProcessScheduler::lotterySelect(const std::vector<PCB *> &readyQueue)
{
    if (readyQueue.empty()){
        return nullptr;
    }
    
    uint64_t total = 0;
    for (auto *p : readyQueue)
    {
        total += std::max(1, p->tickets);
    }
    std::uniform_int_distribution<uint64_t> dist(1, total);
    uint64_t pick = dist(rng);
    for (auto *p : readyQueue)
    {
        uint64_t t = std::max(1, p->tickets);
        if (pick <= t)
            cout << "Processo selecionado na loteria: " << p->name << " com " << p->tickets << " tickets.\n";
            return p;
        pick -= t;
    }
    return readyQueue.front();
}

PCB *ProcessScheduler::first_come_first_served(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;
    return process.front();
}