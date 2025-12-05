#ifndef CONTROL_UNIT_HPP
#define CONTROL_UNIT_HPP

#include <unordered_map>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <bitset>
#include <fstream>

#include "MemoryManager.hpp"
#include "PCB.hpp"
#include "../IO/IOManager.hpp"
#include "datapath/REGISTER_BANK.hpp" // Incluído diretamente para ter a definição completa
#include "datapath/ULA.hpp"
#include "datapath/HASH_REGISTER.hpp"
#include "cache/cache.hpp"
#include "PipelineRegister.hpp"

using std::string;
using std::vector;
using std::deque;
using std::uint32_t;
using std::unique_ptr;
static std::mutex log_mutex;
static std::mutex io_mutex;
static bool forwarding_log_initialized = false;
static constexpr uint32_t END_SENTINEL = 0b11111100000000000000000000000000u;

// Forward declarations
class MemoryManager;
struct PCB;
struct IORequest;

void* Core(MemoryManager &memoryManager, PCB &process, vector<unique_ptr<IORequest>>* ioRequests, std::atomic<bool> &printLock, int schedulerId);

struct ControlContext {
    hw::REGISTER_BANK &registers;
    MemoryManager &memManager;
    vector<unique_ptr<IORequest>> &ioRequests;
    std::atomic<bool> &printLock;
    PCB &process;
    std::atomic<bool> &endProgram;
    std::atomic<bool> &endExecution;
    std::function<void()> flushPipeline;
};

struct Control_Unit {
    deque<Instruction_Data> data;
    hw::Map map;
    std::map<string, int32_t> exMemFwd;
    std::map<string, int32_t> memWbFwd;
    std::atomic<int> global_epoch{0};

    // Simple load-use hazard tracking (per Control_Unit/Core instance)
    std::atomic<bool> loadHazardActive{false};
    std::string loadHazardReg;
    mutable std::mutex loadHazardMutex;
    mutable std::mutex pc_mutex;

    std::unordered_map<string, string> instructionMap = {
        {"add", "000000"}, {"and", "000001"}, {"div", "000010"}, {"mult","000011"},
        {"sub", "000100"}, {"beq", "000101"}, {"bne", "000110"}, {"bgt", "000111"},
        {"bgti","001000"}, {"blt", "001001"}, {"blti","001010"}, {"j", "001011"},
        {"lw", "001100"},  {"sw", "001101"},  {"li", "001110"},  {"la", "001111"},
        {"print", "010000"},{"end", "111111"}
    };

    static string Get_immediate(uint32_t instruction);
    static string Get_destination_Register(uint32_t instruction);
    static string Get_target_Register(uint32_t instruction);
    static string Get_source_Register(uint32_t instruction);

    // Assinatura corrigida para corresponder à implementação
    string Identificacao_instrucao(uint32_t instruction);

    uint32_t FetchInstruction(ControlContext &context, int &capturedEpoch);
    void Decode(uint32_t instruction, Instruction_Data &data);
    void Execute_Aritmetic_Operation(ControlContext &context, Instruction_Data &d);
    void Execute_Operation(Instruction_Data &data, ControlContext &context);
    void Execute_Loop_Operation(Instruction_Data &d, ControlContext &context);
    void Execute(Instruction_Data &data, ControlContext &context);
    void Execute_Immediate_Operation(ControlContext &context, Instruction_Data &data);
    void log_operation(const std::string &msg);
    void Memory_Acess(Instruction_Data &data, ControlContext &context);
    void Write_Back(Instruction_Data &data, ControlContext &context);
    void FlushPipeline(ControlContext &context);
    std::string resolveRegisterName(const std::string &bits) const;
    bool readRegisterWithForwarding(const std::string &name,
                                    Instruction_Data &current,
                                    ControlContext &context,
                                    int32_t &value);
    void markLoadHazard(const std::string &regName);
    void clearLoadHazard(const std::string &regName);
    bool isLoadHazardFor(const Instruction_Data &data) const;
    std::mutex forwardingMutex;
    std::condition_variable forwardingCv;
};

#endif // CONTROL_UNIT_HPP