// control_unit_with_trace.cpp
#include "CONTROL_UNIT.hpp"
#include <chrono>

using namespace std;

void Control_Unit::log_operation(const std::string &msg) {
    std::lock_guard<std::mutex> lock(log_mutex);

    // Imprime no console
    // std::cout << "[LOG] " << msg << std::endl;

    // Cria nome de arquivo temporário aleatório
    static int temp_file_id = 1;  // pode ser mais sofisticado
    std::ostringstream oss;
    oss << "output/temp_" << temp_file_id << ".log";

    std::ofstream fout(oss.str(), std::ios::app);
    if (fout.is_open()) {
        fout << msg << "\n";
    }
}

static void log_forwarding_event(const std::string &msg) {
    std::lock_guard<std::mutex> lock(log_mutex);

    std::ios_base::openmode mode = std::ios::app;
    if (!forwarding_log_initialized) {
        mode = std::ios::trunc;
        forwarding_log_initialized = true;
    }

    std::ofstream fout("output/forwarding_trace.log", mode);
    if (fout.is_open()) {
        fout << msg << "\n";
    }
}


// Funções auxiliares
static uint32_t binaryStringToUint(const std::string &bin) {
    uint32_t value = 0;
    for (char c : bin) {
        value <<= 1;
        if (c == '1') value |= 1u;
        else if (c != '0') throw std::invalid_argument("binaryStringToUint: char nao binario");
    }
    return value;
}

static int32_t signExtend16(uint16_t v) {
    if (v & 0x8000)
        return (int32_t)(0xFFFF0000u | v);
    else
        return (int32_t)(v & 0x0000FFFFu);
}

static std::string regIndexToBitString(uint32_t idx) {
    std::string s(5, '0');
    for (int i = 4; i >= 0; --i) {
        s[4 - i] = ((idx >> i) & 1) ? '1' : '0';
    }
    return s;
}

static std::string toBinStr(uint32_t v, int width) {
    std::string s(width, '0');
    for (int i = 0; i < width; ++i)
        s[width - 1 - i] = ((v >> i) & 1) ? '1' : '0';
    return s;
}

static inline void account_pipeline_cycle(PCB &p) { p.pipeline_cycles.fetch_add(1); }
static inline void account_stage(PCB &p) { p.stage_invocations.fetch_add(1); }

std::string Control_Unit::resolveRegisterName(const std::string &bits) const {
    if (bits.empty()) {
        return "";
    }

    try {
        uint32_t index = binaryStringToUint(bits);
        return this->map.getRegisterName(index);
    } catch (...) {
        return "";
    }
}

bool Control_Unit::readRegisterWithForwarding(const std::string &name,
                                              Instruction_Data &current,
                                              ControlContext &context,
                                              int32_t &value) {
    if (name.empty()) {
        value = 0;
        return true;
    }

    int32_t regValue = static_cast<int32_t>(context.registers.readRegister(name));
    value = regValue;

    // Registradores somente leitura (ex.: zero) não participam do mecanismo de forwarding.
    if (map.isReadOnly(name)) {
        return true;
    }

    std::string sourceLabel;
    {
        std::lock_guard<std::mutex> guard(forwardingMutex);
        auto exIt = exMemFwd.find(name);
        if (exIt != exMemFwd.end()) {
            value = exIt->second;
            sourceLabel = "ALU";
        } else {
            auto memIt = memWbFwd.find(name);
            if (memIt != memWbFwd.end()) {
                value = memIt->second;
                sourceLabel = "LOAD";
            }
        }
    }

    if (!sourceLabel.empty()) {
        std::ostringstream ss;
        ss << "[FWD] reg=" << name << " <- " << sourceLabel
           << " value=" << value;
        log_forwarding_event(ss.str());
    }

    return true;
}

void Control_Unit::markLoadHazard(const std::string &regName) {
    if (regName.empty()) {
        return;
    }
    std::lock_guard<std::mutex> guard(loadHazardMutex);
    loadHazardReg = regName;
    loadHazardActive.store(true, std::memory_order_release);
}

void Control_Unit::clearLoadHazard(const std::string &regName) {
    std::lock_guard<std::mutex> guard(loadHazardMutex);
    if (!regName.empty() && regName != loadHazardReg) {
        return;
    }
    loadHazardReg.clear();
    loadHazardActive.store(false, std::memory_order_release);
}

bool Control_Unit::isLoadHazardFor(const Instruction_Data &data) const {
    std::lock_guard<std::mutex> guard(loadHazardMutex);
    if (!loadHazardActive.load(std::memory_order_acquire)) {
        return false;
    }
    if (loadHazardReg.empty()) {
        return false;
    }
    return (data.sourceRegisterName == loadHazardReg) ||
           (data.targetRegisterName == loadHazardReg);
}

string Control_Unit::Get_immediate(const uint32_t instruction) {
    uint16_t imm = static_cast<uint16_t>(instruction & 0xFFFFu);
    return std::bitset<16>(imm).to_string();
}

string Control_Unit::Get_destination_Register(const uint32_t instruction) {
    uint32_t rd = (instruction >> 11) & 0x1Fu;
    return regIndexToBitString(rd);
}

string Control_Unit::Get_target_Register(const uint32_t instruction) {
    uint32_t rt = (instruction >> 16) & 0x1Fu;
    return regIndexToBitString(rt);
}

string Control_Unit::Get_source_Register(const uint32_t instruction) {
    uint32_t rs = (instruction >> 21) & 0x1Fu;
    return regIndexToBitString(rs);
}

string Control_Unit::Identificacao_instrucao(uint32_t instruction) {
    // Extrai o opcode (bits 31-26)
    uint32_t opcode = (instruction >> 26) & 0x3Fu;

    switch (opcode) {
        // --- Instruções do tipo R (Opcode 0) ---
        case 0x00: { 
            uint32_t funct = instruction & 0x3Fu; // Extrai o campo funct (bits 5-0)
            switch (funct) {
                case 0x20: return "ADD";   // 0b100000
                case 0x22: return "SUB";   // 0b100010
                case 0x24: return "AND";   // 0b100100
                case 0x25: return "OR";    // 0b100101
                case 0x18: return "MULT";  // 0b011000
                case 0x1A: return "DIV";   // 0b011010
                case 0x00: return "SLL";   // 0b000000
                case 0x02: return "SRL";   // 0b000010
                case 0x08: return "JR";    // 0b001000
                default: return "";        // Funct desconhecido
            }
        }

        // --- Instruções do tipo I e J ---
        case 0x02: return "J";      // 0b000010
        case 0x03: return "JAL";    // 0b000011
        case 0x04: return "BEQ";    // 0b000100
        case 0x05: return "BNE";    // 0b000101
        case 0x07: return "BGT";    // 0b000111
        case 0x08: return "ADDI";   // 0b001000
        case 0x09: return "BLT";    // 0b001001 (definição do parser prevalece sobre ADDIU)
        case 0x0A: return "SLTI";   // 0b001010
        case 0x0C: return "ANDI";   // 0b001100
        case 0x0D: return "ORI";    // 0b001101
        case 0x0F: return "LI";     // 0b001111 (definição do parser prevalece sobre LUI)
        case 0x10: return "PRINT";  // 0b010000
        case 0x23: return "LW";     // 0b100011
        case 0x2B: return "SW";     // 0b101011
        case 0x3F: return "END";    // 0b111111

        default: return "";         // Opcode desconhecido
    }
}

uint32_t Control_Unit::FetchInstruction(ControlContext &context, int &capturedEpoch, uint32_t &fetchedPC) {
    account_stage(context.process);

    std::lock_guard<std::mutex> lock(pc_mutex);

    capturedEpoch = global_epoch.load(std::memory_order_relaxed);

    uint32_t pcValue = context.registers.pc.read();

    fetchedPC = pcValue;

    context.registers.mar.write(pcValue);
    uint32_t instr = context.memManager.read(pcValue, context.process);
    context.registers.ir.write(instr);

    if (instr != END_SENTINEL) {
        context.registers.pc.write(pcValue + 4);
    } 
    // else {
    //     context.endProgram.store(true, std::memory_order_relaxed);
    // }

    return instr;
}

void Control_Unit::Decode(uint32_t instruction, Instruction_Data &data) {
    uint32_t saved_pc = data.pc; 
    int saved_epoch = data.epoch;

    data = Instruction_Data{};

    data.pc = saved_pc;
    data.epoch = saved_epoch;

    data.rawInstruction = instruction;
    data.op = Identificacao_instrucao(instruction);

    // R-type
    if (data.op == "ADD" || data.op == "SUB" || data.op == "MULT" || data.op == "DIV") {
        data.source_register = Get_source_Register(instruction);
        data.target_register = Get_target_Register(instruction);
        data.destination_register = Get_destination_Register(instruction);

    // Tipo I: ADDI, ADDIU, LI, LW, LA, SW e branches / immediates customizados
    } else if (data.op == "ADDI" || data.op == "ADDIU" ||
               data.op == "LI" || data.op == "LW" || data.op == "LA" || data.op == "SW" ||
               data.op == "BGTI" || data.op == "BLTI" || data.op == "BEQ" || data.op == "BNE" ||
               data.op == "BGT"  || data.op == "BLT" || data.op == "SLTI" || data.op == "LUI") {

        data.source_register = Get_source_Register(instruction);   // rs
        data.target_register = Get_target_Register(instruction);   // rt (destino para ADDI/LW)
        data.addressRAMResult = Get_immediate(instruction);
        uint16_t imm16 = static_cast<uint16_t>(instruction & 0xFFFFu);
        data.immediate = signExtend16(imm16);

    } else if (data.op == "J") {
        uint32_t instr26 = instruction & 0x03FFFFFFu;
        data.addressRAMResult = std::bitset<26>(instr26).to_string();
        data.immediate = static_cast<int32_t>(instr26);
    } else if (data.op == "PRINT") {
        data.target_register = Get_target_Register(instruction);
        std::string imm = Get_immediate(instruction);
        bool allZero = true;
        for (char c : imm) if (c != '0') { allZero = false; break; }
        if (!allZero) {
            data.addressRAMResult = imm;
            uint16_t imm16 = static_cast<uint16_t>(binaryStringToUint(imm));
            data.immediate = signExtend16(imm16);
        } else {
            data.addressRAMResult.clear();
            data.immediate = 0;
        }
    }

    if (!data.source_register.empty()) {
        data.sourceRegisterName = resolveRegisterName(data.source_register);
    }
    if (!data.target_register.empty()) {
        data.targetRegisterName = resolveRegisterName(data.target_register);
    }
    if (!data.destination_register.empty()) {
        data.destinationRegisterName = resolveRegisterName(data.destination_register);
    }

    auto setWriteIntent = [&](const std::string &regName) {
        if (regName.empty()) {
            return;
        }
        std::lock_guard<std::mutex> guard(forwardingMutex);
        data.writeRegisterName = regName;
        data.writesRegister = true;
    };

    if (data.op == "ADD" || data.op == "SUB" || data.op == "MULT" || data.op == "DIV") {
        setWriteIntent(data.destinationRegisterName);
    } else if (data.op == "ADDI" || data.op == "ADDIU" || data.op == "SLTI" ||
               data.op == "LUI" || data.op == "LI" || data.op == "LW" ||
               data.op == "LA") {
        setWriteIntent(data.targetRegisterName);
    }
}


void Control_Unit::Execute_Immediate_Operation(ControlContext &context, Instruction_Data &data) {
    std::string name_rs = data.sourceRegisterName;
    std::string name_rt = data.targetRegisterName;

    if (name_rt.empty()) {
        return;
    }

    int32_t val_rs = 0;
    if (!readRegisterWithForwarding(name_rs, data, context, val_rs)) {
        throw std::runtime_error("Hazard forwarding falhou para " + name_rs);
    }
    int32_t imm = data.immediate; // já sign-extended

    std::ostringstream ss;

    // ADDI / ADDIU
    if (data.op == "ADDI" || data.op == "ADDIU") {
        ALU alu;
        alu.A = val_rs;
        alu.B = imm;
        alu.op = ADD;
        alu.calculate();
        {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.writeRegisterName = name_rt;
            data.writesRegister = true;
            data.hasAluResult = true;
            data.aluResult = alu.result;
            exMemFwd[name_rt] = alu.result;
        }
        forwardingCv.notify_all();

        // ss << "[IMM] " << data.op << " "
        //    << name_rt << " = " << name_rs << "(" << val_rs << ") + "
        //    << imm << " -> " << alu.result;
        // log_operation(ss.str());
        return;
    }

    // SLTI
    if (data.op == "SLTI") {
        int32_t res = (val_rs < imm) ? 1 : 0;
        {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.writeRegisterName = name_rt;
            data.writesRegister = true;
            data.hasAluResult = true;
            data.aluResult = res;
            exMemFwd[name_rt] = res;
        }
        forwardingCv.notify_all();

        // ss << "[IMM] SLTI " << name_rt << " = (" << name_rs << "(" << val_rs
        //    << ") < " << imm << ") ? 1 : 0 -> " << res;
        // log_operation(ss.str());
        return;
    }

    // LUI
    if (data.op == "LUI") {
        uint32_t uimm = static_cast<uint32_t>(static_cast<uint16_t>(imm));
        int32_t val = static_cast<int32_t>(uimm << 16);
        {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.writeRegisterName = name_rt;
            data.writesRegister = true;
            data.hasAluResult = true;
            data.aluResult = val;
            exMemFwd[name_rt] = val;
        }
        forwardingCv.notify_all();

        // ss << "[IMM] LUI " << name_rt << " = (0x" << std::hex << imm
        //    << " << 16) -> 0x" << val << std::dec;
        // log_operation(ss.str());
        return;
    }

    // LI
    if (data.op == "LI") {
        {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.writeRegisterName = name_rt;
            data.writesRegister = true;
            data.hasAluResult = true;
            data.aluResult = imm;
            exMemFwd[name_rt] = imm;
        }
        forwardingCv.notify_all();

        // ss << "[IMM] LI " << name_rt << " = " << imm;
        // log_operation(ss.str());
        return;
    }

    // Caso não mapeado
    // ss << "[IMM] UNKNOWN OP: " << data.op
    //    << " rs=" << name_rs << " imm=" << imm;
    // log_operation(ss.str());
}

void Control_Unit::Execute_Aritmetic_Operation(ControlContext &context, Instruction_Data &data) {
    std::string name_rs = data.sourceRegisterName;
    std::string name_rt = data.targetRegisterName;
    std::string name_rd = data.destinationRegisterName;

    if (name_rs.empty() || name_rt.empty() || name_rd.empty()) {
        return;
    }

    int32_t val_rs = 0;
    if (!readRegisterWithForwarding(name_rs, data, context, val_rs)) {
        throw std::runtime_error("Hazard forwarding falhou para " + name_rs);
    }

    int32_t val_rt = 0;
    if (!readRegisterWithForwarding(name_rt, data, context, val_rt)) {
        throw std::runtime_error("Hazard forwarding falhou para " + name_rt);
    }

    ALU alu;
    alu.A = val_rs;
    alu.B = val_rt;

    if (data.op == "ADD") alu.op = ADD;
    else if (data.op == "SUB") alu.op = SUB;
    else if (data.op == "MULT") alu.op = MUL;
    else if (data.op == "DIV") alu.op = DIV;
    else return;

    alu.calculate();
    {
        std::lock_guard<std::mutex> guard(forwardingMutex);
        data.writeRegisterName = name_rd;
        data.writesRegister = true;
        data.hasAluResult = true;
        data.aluResult = alu.result;
        exMemFwd[name_rd] = alu.result;
    }
    forwardingCv.notify_all();

    std::ostringstream ss;
    // ss << "[ARIT] " << data.op << " " << name_rd
    //    << " = " << name_rs << "(" << val_rs << ") "
    //    << data.op << " " << name_rt << "(" << val_rt << ") = "
    //    << alu.result;
    // log_operation(ss.str());
}

void Control_Unit::Execute_Operation(Instruction_Data &data, ControlContext &context) {
    if (data.op == "PRINT") {
        // std::cout << "[EXEC] Operação PRINT para o processo PID " << context.process.pid << "\n";
        if (!data.target_register.empty()) {
            string name = this->map.getRegisterName(binaryStringToUint(data.target_register));
            int32_t value = 0;
            if (!readRegisterWithForwarding(name, data, context, value)) {
                throw std::runtime_error("Hazard forwarding falhou para " + name);
            }
            auto req = std::make_unique<IORequest>();
            req->msg = std::to_string(value);
            req->process = &context.process;
            context.process.appendProgramOutput(req->msg);
            {
                std::lock_guard<std::mutex> queueLock(io_mutex);
                context.ioRequests.push_back(std::move(req));
            }

            // TRACE PRINT do registrador
            // std::cout << "[PRINT-REQ] PRINT REG " << name << " valor=" << value
            //           << " (pid=" << context.process.pid << ")\n";

            // if (context.printLock.load(std::memory_order_relaxed)) {
            //     context.process.state.store(State::Blocked);
            //     context.endExecution.store(true);
            // }
        }
    }
}

void Control_Unit::Execute_Loop_Operation(Instruction_Data &data, ControlContext &context) {
    std::string name_rs = data.sourceRegisterName;
    std::string name_rt = data.targetRegisterName;

    int32_t operandA = 0;
    int32_t operandB = 0;

    if (data.op != "J") {
        std::string name_rs = data.sourceRegisterName;
        std::string name_rt = data.targetRegisterName;

        if (name_rs.empty()) {
            return;
        }

        if (!readRegisterWithForwarding(name_rs, data, context, operandA)) {
            throw std::runtime_error("Hazard forwarding falhou para " + name_rs);
        }

        if (!name_rt.empty()) {
            if (!readRegisterWithForwarding(name_rt, data, context, operandB)) {
                throw std::runtime_error("Hazard forwarding falhou para " + name_rt);
            }
        }
    }

    ALU alu;
    alu.A = operandA;
    alu.B = operandB;

    bool jump = false;
    if (data.op == "BEQ") { alu.op = BEQ; alu.calculate(); jump = (alu.result == 1); }
    else if (data.op == "BNE") { alu.op = BNE; alu.calculate(); jump = (alu.result == 1); }
    else if (data.op == "J") { jump = true; }
    else if (data.op == "BLT") { alu.op = BLT; alu.calculate(); jump = (alu.result == 1); }
    else if (data.op == "BGT") { alu.op = BGT; alu.calculate(); jump = (alu.result == 1); }

    if (jump) {
        std::lock_guard<std::mutex> lock(pc_mutex);

        global_epoch.fetch_add(1, std::memory_order_relaxed);
        if (data.op == "J") {
            // std::cout << "[JUMP] Saltando para o endereço da instrução: "
            //   << "0x" << std::hex << binaryStringToUint(data.addressRAMResult) << std::dec << "\n";
            // uint32_t addr = static_cast<uint32_t>(data.immediate);
            // context.registers.pc.write(addr);
            uint32_t target = static_cast<uint32_t>(data.immediate);
            context.registers.pc.write(target);
        } else {
            int32_t offset = data.immediate;
            // uint32_t nextPc = context.registers.pc.read();
            // uint32_t target = static_cast<uint32_t>(nextPc + (offset << 2));
            // context.registers.pc.write(target);
            
            uint32_t target = (data.pc + 4) + (offset << 2);
            
            context.registers.pc.write(target);
        }
        FlushPipeline(context);
        
    }
}

void Control_Unit::Execute(Instruction_Data &data, ControlContext &context) {
    account_stage(context.process);

    auto assignAddress = [&data]() {
        if (data.addressRAMResult.empty()) {
            return false;
        }
        try {
            data.effectiveAddress = binaryStringToUint(data.addressRAMResult);
            data.hasEffectiveAddress = true;
            return true;
        } catch (...) {
            data.hasEffectiveAddress = false;
            return false;
        }
    };

    if (data.op == "END") {
        context.endProgram.store(true, std::memory_order_relaxed);
        return; 
    }

    if (data.op == "LW") {
        if (assignAddress() && !data.targetRegisterName.empty()) {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.pendingMemoryRead = true;
            data.writeRegisterName = data.targetRegisterName;
            data.writesRegister = true;
        }
        return;
    }

    if (data.op == "SW") {
        if (assignAddress() && !data.targetRegisterName.empty()) {
            data.pendingMemoryWrite = true;
            if (!readRegisterWithForwarding(data.targetRegisterName, data, context, data.storeValue)) {
                throw std::runtime_error("Hazard forwarding falhou para " + data.targetRegisterName);
            }
        }
        return;
    }

    if (data.op == "LA") {
        if (assignAddress() && !data.targetRegisterName.empty()) {
            {
                std::lock_guard<std::mutex> guard(forwardingMutex);
                data.writeRegisterName = data.targetRegisterName;
                data.writesRegister = true;
                data.hasAluResult = true;
                data.aluResult = static_cast<int32_t>(data.effectiveAddress);
            }
            forwardingCv.notify_all();
        }
        return;
    }

    // Imediatos / aritmética tipo I
    if (data.op == "ADDI" || data.op == "ADDIU" || data.op == "SLTI" || data.op == "LUI" ||
        data.op == "LI") {
        Execute_Immediate_Operation(context, data);
        return;
    }

    // Tipo R
    if (data.op == "ADD" || data.op == "SUB" || data.op == "MULT" || data.op == "DIV") {
        Execute_Aritmetic_Operation(context, data);
    } else if (data.op == "BEQ" || data.op == "J" || data.op == "BNE" || data.op == "BGT" || data.op == "BGTI" || data.op == "BLT" || data.op == "BLTI") {
        Execute_Loop_Operation(data, context);
    } else if (data.op == "PRINT") {
        Execute_Operation(data, context);
    }
}

void Control_Unit::Memory_Access(Instruction_Data &data, ControlContext &context) {
    account_stage(context.process);

    if (data.pendingMemoryRead && data.hasEffectiveAddress) {
        int value = context.memManager.read(data.effectiveAddress, context.process);
        {
            std::lock_guard<std::mutex> guard(forwardingMutex);
            data.loadResult = value;
            data.hasLoadResult = true;
            data.pendingMemoryRead = false;
            memWbFwd[data.writeRegisterName] = value;
        }
        clearLoadHazard(data.writeRegisterName);
        forwardingCv.notify_all();
    }

    if (data.pendingMemoryWrite && data.hasEffectiveAddress) {
        context.memManager.write(data.effectiveAddress, data.storeValue, context.process);
        data.pendingMemoryWrite = false;
    }

    if (data.op == "PRINT" && data.target_register.empty() && !data.addressRAMResult.empty()) {
        uint32_t addr = binaryStringToUint(data.addressRAMResult);
        int value = context.memManager.read(addr, context.process);
        auto req = std::make_unique<IORequest>();
        req->msg = std::to_string(value);
        req->process = &context.process;
        context.process.appendProgramOutput(req->msg);
        {
            std::lock_guard<std::mutex> queueLock(io_mutex);
            context.ioRequests.push_back(std::move(req));
        }

        // if (context.printLock.load(std::memory_order_relaxed)) {
        //     context.process.state.store(State::Blocked);
        //     context.endExecution.store(true);
        // }
    }
}

void Control_Unit::Write_Back(Instruction_Data &data, ControlContext &context) {
    account_stage(context.process);
    if (!data.writesRegister || data.writeRegisterName.empty()) {
        return;
    }

    int32_t value = 0;
    bool hasValue = false;

    if (data.hasLoadResult) {
        value = data.loadResult;
        hasValue = true;
    } else if (data.hasAluResult) {
        value = data.aluResult;
        hasValue = true;
    }

    if (!hasValue) {
        return;
    }

    context.registers.writeRegister(data.writeRegisterName, static_cast<uint32_t>(value));
    {
        std::lock_guard<std::mutex> guard(forwardingMutex);
        if (data.hasAluResult) {
            exMemFwd.erase(data.writeRegisterName);
        }
        if (data.hasLoadResult) {
            memWbFwd.erase(data.writeRegisterName);
        }
        data.writesRegister = false;
        data.writeRegisterName.clear();
        data.hasLoadResult = false;
        data.hasAluResult = false;
    }
    forwardingCv.notify_all();
}

void Control_Unit::FlushPipeline(ControlContext &context) {
    if (context.flushPipeline) {
        context.flushPipeline();
    }

    std::lock_guard<std::mutex> guard(loadHazardMutex);
    loadHazardReg.clear();
    loadHazardActive.store(false, std::memory_order_release);
}

// A função Core agora utiliza um buffer entre estágios e cinco threads dedicadas
void* Core(MemoryManager &memoryManager, PCB &process, vector<unique_ptr<IORequest>>* ioRequests, std::atomic<bool> &printLock, int schedulerId) {
    Control_Unit UC;

    if (process.startTime.load() == 0) {
    process.startTime.store(process.timeStamp); // ou use um contador global do simulador
}
    std::atomic<bool> endProgram{false};
    std::atomic<bool> endExecution{false};
    std::mutex pcMutex;

    ControlContext context{ process.regBank, memoryManager, *ioRequests, printLock, process, endProgram, endExecution };

    PipelineRegister ifId;
    PipelineRegister idEx;
    PipelineRegister exMem;
    PipelineRegister memWb;

    std::atomic<uint64_t> progressCounter{0};
    auto markProgress = [&progressCounter]() {
        progressCounter.fetch_add(1, std::memory_order_relaxed);
    };

    auto logRegs = [&]() {
        std::cerr << "[watchdog] pid=" << process.pid
                  << " ifId tok=" << ifId.debugHasToken() << " stop=" << ifId.debugStopped()
                  << " idEx tok=" << idEx.debugHasToken() << " stop=" << idEx.debugStopped()
                  << " exMem tok=" << exMem.debugHasToken() << " stop=" << exMem.debugStopped()
                  << " memWb tok=" << memWb.debugHasToken() << " stop=" << memWb.debugStopped()
                  << " endExec=" << endExecution.load(std::memory_order_relaxed)
                  << " endProg=" << context.endProgram.load(std::memory_order_relaxed)
                  << " epoch=" << UC.global_epoch.load(std::memory_order_relaxed)
                  << " progress=" << progressCounter.load(std::memory_order_relaxed)
                  << "\n";
    };

    context.flushPipeline = [&]() {
        ifId.flush();
        idEx.flush();
    };


    std::atomic<int> issuedCycles{0};

    auto makeDrainToken = [&](bool programEndedFlag) {
        PipelineToken drain;
        drain.terminate = true;
        drain.programEnded = programEndedFlag;
        return drain;
    };

    std::atomic<bool> stopWatchdog{false};

    std::thread watchdogThread([&]() {
        // Disable watchdog for FCFS (schedulerId == 4) to prevent premature termination of long-running processes
        // if (schedulerId != 0 && schedulerId != 2) {
        //     // std::cout << "[Watchdog] Disabled for FCFS (schedulerId=" << schedulerId << ")\n";
        //     return;
        // }

        uint64_t last = progressCounter.load(std::memory_order_relaxed);
        int stuckRounds = 0;
        while (!stopWatchdog.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            uint64_t now = progressCounter.load(std::memory_order_relaxed);
            if (now == last) {
                ++stuckRounds;
                // std::cerr << "[watchdog] pipeline não avançou por 5ms (pid="
                //           << process.pid << ")\n";
                // logRegs();

                bool idle = !ifId.debugHasToken() && !idEx.debugHasToken() &&
                            !exMem.debugHasToken() && !memWb.debugHasToken();
                if (idle && !endExecution.load(std::memory_order_relaxed) &&
                    !context.endProgram.load(std::memory_order_relaxed)) {
                    endExecution.store(true, std::memory_order_relaxed);
                    ifId.push(makeDrainToken(false));
                    markProgress();
                    ifId.stop();
                    idEx.stop();
                    exMem.stop();
                    memWb.stop();
                }


                // Se o pipeline não estiver ocioso, mas permanecer travado, force uma
                // reinicialização para evitar livelock (por exemplo, um thread de estágio esperando para sempre)
                if (!idle && stuckRounds >= 3 &&
                    !endExecution.load(std::memory_order_relaxed) &&
                    !context.endProgram.load(std::memory_order_relaxed)) {
                    // std::cerr << "[watchdog] forçando reset do pipeline após travamento (pid="
                    //           << process.pid << ")\n";
                    UC.clearLoadHazard("");

                    PipelineToken stuck = ifId.debugPeek();
                    if (stuck.entry && stuck.valid) {
                        context.registers.pc.write(stuck.entry->pc);
                    }

                    endExecution.store(true, std::memory_order_relaxed);
                    ifId.flush();
                    idEx.flush();
                    exMem.flush();
                    memWb.flush();
                    ifId.push(makeDrainToken(context.endProgram.load(std::memory_order_relaxed)));
                    markProgress();
                    ifId.stop();
                    idEx.stop();
                    exMem.stop();
                    memWb.stop();
                }
            } else {
                stuckRounds = 0;
            }
            last = now;
        }
    });

    std::thread fetchThread([&]() {
        bool drainSent = false;
        while (true) {
            if (endExecution.load(std::memory_order_relaxed)) {
                break;
            }

            if (context.endProgram.load(std::memory_order_relaxed)) {
                drainSent = true;
                ifId.push(makeDrainToken(true));
                markProgress();
                break;
            }

            int fetchEpoch = 0;
            uint32_t fetchedPC = 0;

            uint32_t instruction = UC.FetchInstruction(context, fetchEpoch, fetchedPC);

            if (instruction == END_SENTINEL) {
                // Adicione um END token e um drain; deixe o estágio Execute definir endProgram quando executar END.
                PipelineToken token;
                token.entry = &UC.data.emplace_back();
                token.entry->epoch = fetchEpoch;
                token.entry->pc = fetchedPC;
                token.valid = true;
                token.instruction = instruction;
                ifId.push(token);
                markProgress();

                ifId.push(makeDrainToken(false));
                markProgress();
                break;
            }

            PipelineToken token;
            token.entry = &UC.data.emplace_back();

            token.entry->epoch = fetchEpoch;
            
            token.entry->pc = fetchedPC;
            token.valid = true;
            token.instruction = instruction;
            ifId.push(token);
            markProgress();

            issuedCycles.fetch_add(1, std::memory_order_relaxed);
            account_pipeline_cycle(process);

            if ((schedulerId == 0 || schedulerId == 2 ) && issuedCycles.load(std::memory_order_relaxed) >= process.quantum) {
                endExecution.store(true, std::memory_order_relaxed);
                break;
            }
        }

        if (!drainSent) {
            bool programEndedFlag = context.endProgram.load(std::memory_order_relaxed);
            ifId.push(makeDrainToken(programEndedFlag));
            markProgress();
        }

        // Se uma preempção ocorreu (endExecution) sem o fim do programa, acorde o
        // pipeline com um token de drenagem e sinais de parada não destrutivos para que
        // os threads de estágio saiam de seus loops de espera sem descartar o trabalho em andamento.
        if (endExecution.load(std::memory_order_relaxed) &&
            !context.endProgram.load(std::memory_order_relaxed)) {
            ifId.push(makeDrainToken(false));
            markProgress();
            ifId.stop();
            idEx.stop();
            exMem.stop();
            memWb.stop();
        }
    });

    std::thread decodeThread([&]() {
        PipelineToken token;
        while (ifId.pop(token)) {
            markProgress();
            if (token.terminate) {
                idEx.push(token);
                markProgress();
                break;
            }
            if (!token.valid) {
                continue;
            }

            int local_epoch = token.entry->epoch;
            if (local_epoch != UC.global_epoch.load(std::memory_order_relaxed)) {
                // Token veio de um epoch obsoleto (por exemplo, após branch/flush). Qualquer
                // hazard de load pendente desse epoch pode ser limpo com segurança; caso contrário,
                // o Decode pode ficar parado para sempre esperando um load que nunca chegará ao MEM/WB.
                UC.clearLoadHazard("");
                continue;
            }

            account_stage(context.process);
            UC.Decode(token.instruction, *token.entry);

            token.entry->epoch = local_epoch;

            if (local_epoch != UC.global_epoch.load(std::memory_order_relaxed)) {
                continue;
            }

            auto hazardStart = std::chrono::steady_clock::now();
            while (UC.isLoadHazardFor(*token.entry)) {
                // Escape de segurança: se um hazard de load nunca limpar (por exemplo,
                // a instrução produtora foi descartada ou preemptada), quebre o
                // stall para evitar deadlock. Isso preserva progresso, ao custo de
                // possivelmente ler um valor desatualizado.
                auto now = std::chrono::steady_clock::now();
                if (now - hazardStart > std::chrono::milliseconds(50)) {
                    UC.clearLoadHazard(token.entry->targetRegisterName);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }

            if (token.entry->op == "LW" && !token.entry->targetRegisterName.empty()) {
                UC.markLoadHazard(token.entry->targetRegisterName);
            }

            token.instruction = 0;
            idEx.push(token);
            markProgress();
        }
    });

    std::thread executeThread([&]() {
        PipelineToken token;
        while (idEx.pop(token)) {
            markProgress();
            if (token.terminate) {
                exMem.push(token);
                markProgress();
                break;
            }
            if (!token.valid) {
                continue;
            }

            if (token.entry->epoch != UC.global_epoch.load(std::memory_order_relaxed)) {
                // A instrução ficou obsoleta após mudança de fluxo; limpe qualquer
                // hazard de load pendente para o Decode não ficar parado.
                UC.clearLoadHazard("");
                continue;
            }

            UC.Execute(*token.entry, context);
            exMem.push(token);
            markProgress();
        }
    });

    std::thread memoryThread([&]() {
        PipelineToken token;
        while (exMem.pop(token)) {
            markProgress();
            if (token.terminate) {
                memWb.push(token);
                markProgress();
                break;
            }
            if (!token.valid) {
                continue;
            }
            UC.Memory_Access(*token.entry, context);
            memWb.push(token);
            markProgress();
        }
    });

    std::thread writeThread([&]() {
        PipelineToken token;
        while (memWb.pop(token)) {
            markProgress();
            if (token.terminate) {
                if (token.programEnded) {
                    context.endProgram.store(true, std::memory_order_relaxed);
                }
                break;
            }
            if (!token.valid) {
                continue;
            }
            UC.Write_Back(*token.entry, context);
        }
    });

    fetchThread.join();
    decodeThread.join();
    executeThread.join();
    memoryThread.join();
    writeThread.join();

    stopWatchdog.store(true, std::memory_order_relaxed);
    watchdogThread.join();

    process.timeStamp += issuedCycles.load(std::memory_order_relaxed);

    if (context.endProgram.load(std::memory_order_relaxed)) {
        process.state.store(State::Finished);
    } else if (process.state.load() != State::Blocked) {
        process.state.store(State::Ready);
    }

    // === DUMP FINAL DOS REGISTRADORES ===
    {
        const vector<string> fallback_names = {
            "zero","at","v0","v1","a0","a1","a2","a3",
            "t0","t1","t2","t3","t4","t5","t6","t7",
            "s0","s1","s2","s3","s4","s5","s6","s7",
            "t8","t9","k0","k1","gp","sp","fp","ra"
        };

        try {
            for (uint32_t i = 0; i < 32; ++i) {
                string name;
                if (i < fallback_names.size()) name = fallback_names[i];
                else name = "r" + to_string(i);
                (void)context.registers.readRegister(name);
            }
        } catch (...) {
            // Ignora erros durante o dump
        }

        // std::cout << "PC = " << context.registers.pc.read() << "\n";
        // std::cout << "IR = 0x" << std::hex << context.registers.ir.read() << std::dec
        //           << " (" << toBinStr(context.registers.ir.read(), 32) << ")\n";
        // std::cout << "========================================\n\n";
    }

   // Depois de todas as threads terem feito join e atualizado timeStamp
    process.burstTime.fetch_add(issuedCycles.load(std::memory_order_relaxed), std::memory_order_relaxed);

    return nullptr;
}
