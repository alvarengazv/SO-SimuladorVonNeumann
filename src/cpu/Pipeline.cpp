#include "Pipeline.hpp"
#include "GlobalClock.hpp"

Pipeline::Pipeline(int pipelineId) : pipelineId_(pipelineId) {
    // Register this pipeline with the GlobalClock
    if (useGlobalClock_) {
        GlobalClock::instance().registerPipeline(pipelineId_, STAGE_COUNT);
    }
    
    fetchThread = std::thread(&Pipeline::fetchLoop, this);
    decodeThread = std::thread(&Pipeline::decodeLoop, this);
    executeThread = std::thread(&Pipeline::executeLoop, this);
    memoryThread = std::thread(&Pipeline::memoryLoop, this);
    writeThread = std::thread(&Pipeline::writeLoop, this);
}

Pipeline::~Pipeline() {
    stop();
}

void Pipeline::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop_threads = true;
    }
    cv_start.notify_all();
    
    // Unregister from GlobalClock
    if (useGlobalClock_) {
        GlobalClock::instance().unregisterPipeline(pipelineId_);
    }
    
    if (fetchThread.joinable()) fetchThread.join();
    if (decodeThread.joinable()) decodeThread.join();
    if (executeThread.joinable()) executeThread.join();
    if (memoryThread.joinable()) memoryThread.join();
    if (writeThread.joinable()) writeThread.join();
}

void Pipeline::recordStageCycle(PCB& process, PipelineStage stage, uint64_t cycle) {
    // Track per-stage cycles (for breakdown analysis)
    switch (stage) {
        case STAGE_FETCH:
            process.cycleMetrics.fetchCycles.fetch_add(1, std::memory_order_relaxed);
            // Count wall-clock cycle on FETCH (represents pipeline advancement)
            // Each fetch = 1 wall-clock cycle (when pipelined, multiple stages execute per cycle)
            process.cycleMetrics.wallClockCycles.fetch_add(1, std::memory_order_relaxed);
            break;
        case STAGE_DECODE:
            process.cycleMetrics.decodeCycles.fetch_add(1, std::memory_order_relaxed);
            break;
        case STAGE_EXECUTE:
            process.cycleMetrics.executeCycles.fetch_add(1, std::memory_order_relaxed);
            break;
        case STAGE_MEMORY:
            process.cycleMetrics.memoryCycles.fetch_add(1, std::memory_order_relaxed);
            break;
        case STAGE_WRITEBACK:
            process.cycleMetrics.writebackCycles.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }
    
    // totalCycles counts ALL stage work (will equal 5 * instructions for complete execution)
    process.cycleMetrics.totalCycles.fetch_add(1, std::memory_order_relaxed);
}

void Pipeline::run(MemoryManager &memoryManager, PCB &process, vector<unique_ptr<IORequest>>* ioRequests, std::atomic<bool> &printLock, int schedulerId) {
    std::atomic<bool> endProgram{false};
    std::atomic<bool> endExecution{false};

    ControlContext context{ process.regBank, memoryManager, *ioRequests, printLock, process, endProgram, endExecution, nullptr };

    context.flushPipeline = [&]() {
        ifId.flush();
        idEx.flush();
        exMem.flush();  // Also flush later stages to prevent commits after blocking
        memWb.flush();
    };

    // Reset pipeline state
    UC.reset();
    ifId.reset();
    idEx.reset();
    exMem.reset();
    memWb.reset();
    ifId.flush();
    idEx.flush();
    exMem.flush();
    memWb.flush();
    
    // Record start cycle for this process
    if (useGlobalClock_) {
        process.cycleMetrics.startCycle = GlobalClock::instance().currentCycle();
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        currentContext = &context;
        work_generation++;
        active_threads = 5;
    }
    cv_start.notify_all();

    // Wait for completion
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv_done.wait(lock, [&]() { return active_threads == 0; });
        currentContext = nullptr;
    }
    
    // Record end cycle
    if (useGlobalClock_) {
        process.cycleMetrics.endCycle = GlobalClock::instance().currentCycle();
    }

    if (context.resumePcValid.load(std::memory_order_relaxed)) {
        uint32_t resume = context.resumePc.load(std::memory_order_relaxed);
        std::cout << "[DEBUG] Restoring PC for pid=" << process.pid 
                  << " from " << context.registers.pc.read() 
                  << " to resumePC=" << resume << "\n";
        context.registers.pc.write(resume);
        context.resumePcValid.store(false, std::memory_order_relaxed);
        context.endProgram.store(false, std::memory_order_relaxed); // Clear endProgram since we're resuming
    }

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

        std::cout << "PC = " << context.registers.pc.read() << "\n";
        std::cout << "IR = 0x" << std::hex << context.registers.ir.read() << std::dec
                  << " (" << UC.toBinStr(context.registers.ir.read(), 32) << ")\n";
        std::cout << "========================================\n\n";
    }
}

void Pipeline::fetchLoop() {
    int my_gen = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_start.wait(lock, [&]() { return work_generation > my_gen || stop_threads; });
            if (stop_threads) return;
            my_gen = work_generation;
        }

        // Fetch Logic
        ControlContext& context = *currentContext;
        std::atomic<int> issuedCycles{0};
        bool drainSent = false;

        auto makeDrainToken = [&](bool programEndedFlag) {
            PipelineToken drain;
            drain.terminate = true;
            drain.programEnded = programEndedFlag;
            return drain;
        };

        while (true) {
            // Wait for clock cycle if using GlobalClock
            uint64_t currentCycle = 0;
            if (useGlobalClock_) {
                currentCycle = GlobalClock::instance().waitForCycleStart(pipelineId_, STAGE_FETCH);
            }
            
            if (context.endExecution.load(std::memory_order_relaxed)) {
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_FETCH);
                }
                break;
            }

            uint32_t instruction = UC.FetchInstruction(context);
            
            // Check if fetch was aborted due to branch
            if (instruction == END_SENTINEL) {
                // END_SENTINEL could be a real END instruction or branch redirect
                // Wait for in-flight instructions to potentially redirect via branch
                for (int i = 0; i < 5 && !context.branchTaken.load(std::memory_order_acquire); ++i) {
                    std::this_thread::yield();
                }
                if (context.branchTaken.load(std::memory_order_acquire)) {
                    context.branchTaken.store(false, std::memory_order_release);
                    context.endProgram.store(false, std::memory_order_release); // Branch overrides END
                    context.process.cycleMetrics.fetchStalls.fetch_add(1, std::memory_order_relaxed);
                    if (useGlobalClock_) {
                        GlobalClock::instance().stageComplete(pipelineId_, STAGE_FETCH);
                    }
                    std::cout << "[END_SENTINEL] pid=" << context.process.pid 
                              << " branch override - retrying fetch from PC=" << context.registers.pc.read() << "\n";
                    continue; // Retry fetch with new PC
                }
            }

            if (context.endProgram.load(std::memory_order_relaxed)) {
                drainSent = true;
                ifId.push(makeDrainToken(true));
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_FETCH);
                }
                break;
            }

            PipelineToken token;
            token.pc = context.registers.mar.read();
            token.entry = &UC.data.emplace_back();
            token.valid = true;
            token.instruction = instruction;
            token.fetchCycle = currentCycle;  // Record fetch cycle
            ifId.push(token);

            issuedCycles.fetch_add(1, std::memory_order_relaxed);
            UC.account_pipeline_cycle(context.process);
            recordStageCycle(context.process, STAGE_FETCH, currentCycle);
            
            // Signal stage completion for this cycle
            if (useGlobalClock_) {
                GlobalClock::instance().stageComplete(pipelineId_, STAGE_FETCH);
            }

            if (context.process.quantum > 0 && issuedCycles.load(std::memory_order_relaxed) >= context.process.quantum) {
                context.endExecution.store(true, std::memory_order_relaxed);
                break;
            }
        }

        if (!drainSent) {
            bool programEndedFlag = context.endProgram.load(std::memory_order_relaxed);
            ifId.push(makeDrainToken(programEndedFlag));
        }

        context.process.timeStamp += issuedCycles.load(std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_threads--;
            if (active_threads == 0) cv_done.notify_one();
        }
    }
}

void Pipeline::decodeLoop() {
    int my_gen = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_start.wait(lock, [&]() { return work_generation > my_gen || stop_threads; });
            if (stop_threads) return;
            my_gen = work_generation;
        }

        ControlContext& context = *currentContext;
        PipelineToken token;
        while (ifId.pop(token)) {
            // Wait for clock cycle if using GlobalClock
            uint64_t currentCycle = 0;
            if (useGlobalClock_) {
                currentCycle = GlobalClock::instance().waitForCycleStart(pipelineId_, STAGE_DECODE);
            }
            
            if (token.terminate) {
                idEx.push(token);
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_DECODE);
                }
                break;
            }
            if (!token.valid) {
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_DECODE);
                }
                continue;
            }
            UC.account_stage(context.process);
            UC.Decode(token.instruction, *token.entry, context);
            token.entry->instructionAddress = token.pc;
            token.instruction = 0;
            token.decodeCycle = currentCycle;  // Record decode cycle
            
            recordStageCycle(context.process, STAGE_DECODE, currentCycle);
            
            idEx.push(token);
            
            if (useGlobalClock_) {
                GlobalClock::instance().stageComplete(pipelineId_, STAGE_DECODE);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_threads--;
            if (active_threads == 0) cv_done.notify_one();
        }
    }
}

void Pipeline::executeLoop() {
    int my_gen = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_start.wait(lock, [&]() { return work_generation > my_gen || stop_threads; });
            if (stop_threads) return;
            my_gen = work_generation;
        }

        ControlContext& context = *currentContext;
        PipelineToken token;
        while (idEx.pop(token)) {
            // Wait for clock cycle if using GlobalClock
            uint64_t currentCycle = 0;
            if (useGlobalClock_) {
                currentCycle = GlobalClock::instance().waitForCycleStart(pipelineId_, STAGE_EXECUTE);
            }
            
            if (token.terminate) {
                exMem.push(token);
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_EXECUTE);
                }
                break;
            }
            if (!token.valid) {
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_EXECUTE);
                }
                continue;
            }
            UC.Execute(*token.entry, context);
            token.executeCycle = currentCycle;  // Record execute cycle
            
            recordStageCycle(context.process, STAGE_EXECUTE, currentCycle);
            
            exMem.push(token);
            
            if (useGlobalClock_) {
                GlobalClock::instance().stageComplete(pipelineId_, STAGE_EXECUTE);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_threads--;
            if (active_threads == 0) cv_done.notify_one();
        }
    }
}

void Pipeline::memoryLoop() {
    int my_gen = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_start.wait(lock, [&]() { return work_generation > my_gen || stop_threads; });
            if (stop_threads) return;
            my_gen = work_generation;
        }

        ControlContext& context = *currentContext;
        PipelineToken token;
        while (exMem.pop(token)) {
            // Wait for clock cycle if using GlobalClock
            uint64_t currentCycle = 0;
            if (useGlobalClock_) {
                currentCycle = GlobalClock::instance().waitForCycleStart(pipelineId_, STAGE_MEMORY);
            }
            
            if (token.terminate) {
                memWb.push(token);
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_MEMORY);
                }
                break;
            }
            if (!token.valid) {
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_MEMORY);
                }
                continue;
            }
            UC.Memory_Acess(*token.entry, context);
            token.memoryCycle = currentCycle;  // Record memory cycle
            
            recordStageCycle(context.process, STAGE_MEMORY, currentCycle);
            
            memWb.push(token);
            
            if (useGlobalClock_) {
                GlobalClock::instance().stageComplete(pipelineId_, STAGE_MEMORY);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_threads--;
            if (active_threads == 0) cv_done.notify_one();
        }
    }
}

void Pipeline::writeLoop() {
    int my_gen = 0;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv_start.wait(lock, [&]() { return work_generation > my_gen || stop_threads; });
            if (stop_threads) return;
            my_gen = work_generation;
        }

        ControlContext& context = *currentContext;
        PipelineToken token;
        while (memWb.pop(token)) {
            // Wait for clock cycle if using GlobalClock
            uint64_t currentCycle = 0;
            if (useGlobalClock_) {
                currentCycle = GlobalClock::instance().waitForCycleStart(pipelineId_, STAGE_WRITEBACK);
            }
            
            if (token.terminate) {
                if (token.programEnded) {
                    context.endProgram.store(true, std::memory_order_relaxed);
                }
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_WRITEBACK);
                }
                break;
            }
            if (!token.valid) {
                if (useGlobalClock_) {
                    GlobalClock::instance().stageComplete(pipelineId_, STAGE_WRITEBACK);
                }
                continue;
            }
            UC.Write_Back(*token.entry, context);
            token.writebackCycle = currentCycle;  // Record writeback cycle
            
            recordStageCycle(context.process, STAGE_WRITEBACK, currentCycle);
            
            // Count completed instruction
            context.process.cycleMetrics.instructionsCompleted.fetch_add(1, std::memory_order_relaxed);
            
            if (useGlobalClock_) {
                GlobalClock::instance().stageComplete(pipelineId_, STAGE_WRITEBACK);
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            active_threads--;
            if (active_threads == 0) cv_done.notify_one();
        }
    }
}