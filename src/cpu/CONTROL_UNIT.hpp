#ifndef CONTROL_UNIT_HPP
#define CONTROL_UNIT_HPP

#include "datapath/REGISTER_BANK.hpp" // Incluído diretamente para ter a definição completa
#include "datapath/ULA.hpp"
#include "datapath/HASH_REGISTER.hpp"
#include "cache/cache.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>

using std::string;
using std::vector;
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

void* Core(MemoryManager &memoryManager, PCB &process, vector<unique_ptr<IORequest>>* ioRequests, std::atomic<bool> &printLock);

struct Instruction_Data {
    string source_register;
    string target_register;
    string destination_register;
    string op;
    string addressRAMResult;
    uint32_t rawInstruction = 0;
    int32_t immediate = 0;
    std::string sourceRegisterName;
    std::string targetRegisterName;
    std::string destinationRegisterName;
    std::string writeRegisterName;
    bool writesRegister = false;
    bool hasAluResult = false;
    int32_t aluResult = 0;
    bool pendingMemoryRead = false;
    bool pendingMemoryWrite = false;
    bool hasEffectiveAddress = false;
    uint32_t effectiveAddress = 0;
    int32_t loadResult = 0;
    bool hasLoadResult = false;
    int32_t storeValue = 0;
};

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
    vector<Instruction_Data> data;
    hw::Map map;

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

    uint32_t FetchInstruction(ControlContext &context);
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
    int instructionIndex(const Instruction_Data &entry) const;
    std::mutex forwardingMutex;
    std::condition_variable forwardingCv;
};

struct PipelineToken {
    Instruction_Data *entry = nullptr;
    uint32_t instruction = 0;
    bool valid = false;
    bool terminate = false;
    bool programEnded = false;
};

class PipelineRegister {
public:
    void push(const PipelineToken &token) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return !hasToken_ || stopped_; });
        if (stopped_) {
            return;
        }
        stored_ = token;
        hasToken_ = true;
        cv_.notify_all();
    }

    bool pop(PipelineToken &out) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return hasToken_ || stopped_; });
        if (!hasToken_) {
            return false;
        }
        out = stored_;
        hasToken_ = false;
        cv_.notify_all();
        return true;
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        hasToken_ = false;
        stored_ = PipelineToken{};
        cv_.notify_all();
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        hasToken_ = false;
        stored_ = PipelineToken{};
        cv_.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !hasToken_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    PipelineToken stored_{};
    bool hasToken_ = false;
    bool stopped_ = false;
};

#endif // CONTROL_UNIT_HPP