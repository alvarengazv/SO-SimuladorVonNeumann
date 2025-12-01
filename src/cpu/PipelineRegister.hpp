#ifndef PIPELINE_REGISTER_HPP
#define PIPELINE_REGISTER_HPP

#include <mutex>
#include <string>
#include <condition_variable>

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
    bool hasEffectiveAddress = false;
    uint32_t effectiveAddress = 0;
    int32_t loadResult = 0;
    bool hasLoadResult = false;
    int32_t storeValue = 0;
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
    void push(const PipelineToken &token);
    bool pop(PipelineToken &out);
    void flush();
    void stop();
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    PipelineToken stored_{};
    bool hasToken_ = false;
    bool stopped_ = false;
};

#endif