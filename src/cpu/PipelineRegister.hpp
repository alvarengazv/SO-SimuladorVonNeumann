#ifndef PIPELINE_REGISTER_HPP
#define PIPELINE_REGISTER_HPP

#include <mutex>
#include <string>
#include <condition_variable>
#include <cstdint>

using namespace std;

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
    uint32_t instructionAddress = 0;
    bool hasEffectiveAddress = false;
    uint32_t effectiveAddress = 0;
    int32_t loadResult = 0;
    bool hasLoadResult = false;
    int32_t storeValue = 0;
};

struct PipelineToken {
    Instruction_Data *entry = nullptr;
    uint32_t instruction = 0;
    uint32_t pc = 0;
    bool valid = false;
    bool terminate = false;
    bool programEnded = false;
    
    // Cycle tracking for metrics
    uint64_t fetchCycle = 0;      // Cycle when instruction was fetched
    uint64_t decodeCycle = 0;     // Cycle when instruction was decoded
    uint64_t executeCycle = 0;    // Cycle when instruction was executed
    uint64_t memoryCycle = 0;     // Cycle when memory access completed
    uint64_t writebackCycle = 0;  // Cycle when writeback completed
};

class PipelineRegister {
public:
    void push(const PipelineToken &token);
    bool pop(PipelineToken &out);
    void flush();
    void stop();
    bool empty() const;
    void reset();  // Reset for new run

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    PipelineToken stored_{};
    bool hasToken_ = false;
    bool stopped_ = false;
};

#endif