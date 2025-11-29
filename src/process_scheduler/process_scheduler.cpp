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
        this->setTimeStamp();
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
        int min_val = 10;
        int max_val = 150;
        unsigned seed = chrono::system_clock::now().time_since_epoch().count();
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
        unsigned seed = chrono::system_clock::now().time_since_epoch().count();
        mt19937 rng(seed);

        uniform_int_distribution<int> dist(min_val, max_val);

        int random_number = dist(rng);

        this->process.at(i)->priority = random_number;
        this->process.at(i)->quantum = 2147483647;
    }
}

void ProcessScheduler::setTimeStamp()
{
    for (int i = 0; i < this->process.size(); i++)
    {
        this->process.at(i)->timeStamp = 0;
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
        return shortest_remainign_time_first(process);
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
        if (p->quantum < selected_process->quantum)
        {
            selected_process = p;
        }
    }
    return selected_process;
}

PCB *ProcessScheduler::shortest_remainign_time_first(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;

    PCB *selected_process = nullptr;
    int min_remaining_time = std::numeric_limits<int>::max();

    for (const auto &p : process)
    {
        int remaining_time = p->quantum - p->timeStamp;

        if (remaining_time < min_remaining_time)
        {
            min_remaining_time = remaining_time;
            selected_process = p;
        }
    }

    return (selected_process != nullptr) ? selected_process : process.front();
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

PCB *ProcessScheduler::first_come_first_served(vector<PCB *> process)
{
    if (process.empty())
        return nullptr;
    return process.front();
}