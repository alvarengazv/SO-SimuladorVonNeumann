#include "metrics.hpp"

void print_metrics(const PCB &pcb) {
    auto programOutput = pcb.snapshotProgramOutput();
    
    std::cout << "\n--- METRICAS FINAIS DO PROCESSO " << pcb.pid << " ---\n";
    std::cout << "Nome do Processo:       " << pcb.name << "\n";
    std::cout << "Estado Final:           "
              << (pcb.state.load() == State::Finished ? "Finished" : "Incomplete") << "\n";
    std::cout << "Timestamp Final:        " << pcb.timeStamp << "\n";
    std::cout << "Ciclos de Pipeline:     " << pcb.pipeline_cycles.load() << "\n";
    std::cout << "Ciclos de IO:           " << pcb.io_cycles.load() << "\n";
    std::cout << "Total de Acessos a Mem: " << pcb.mem_accesses_total.load() << "\n";
    std::cout << "  - Leituras:             " << pcb.mem_reads.load() << "\n";
    std::cout << "  - Escritas:             " << pcb.mem_writes.load() << "\n";
    std::cout << "Acessos a Cache L1:     " << pcb.cache_mem_accesses.load() << "\n";
    std::cout << "  - Reads:     " << pcb.cache_read_accesses.load() << "\n";
    std::cout << "     - Hits:    " << pcb.cache_read_hits.load() << "\n";
    std::cout << "     - Misses:    " << pcb.cache_read_misses.load() << "\n";
    std::cout << "  - Writes:    " << pcb.cache_write_accesses.load() << "\n";
    std::cout << "     - Hits:    " << pcb.cache_write_hits.load() << "\n";
    std::cout << "     - Misses:    " << pcb.cache_write_misses.load() << "\n";
    std::cout << "Acessos a Mem Principal:" << pcb.primary_mem_accesses.load() << "\n";
    std::cout << "Acessos a Mem Secundaria:" << pcb.secondary_mem_accesses.load() << "\n";
    std::cout << "Ciclos Totais de Memoria: " << pcb.memory_cycles.load() << "\n";
    std::cout << "Tempo Total de Execução:  " << pcb.totalTimeExecution() << "\n";
    std::cout << "Cores Utilizados:        ";
    for (const auto& core : pcb.coresAssigned) {
        std::cout << core << " ";
    }
    std::cout << "\nSaída do Programa (PID " << pcb.pid << "):\n";

    if (programOutput.empty()) {
        std::cout << "  (Sem saída registrada)\n";
    } else {
        for (const auto &line : programOutput) {
            std::cout << "  -> " << line << "\n";
        }
    }
    std::cout << "\n------------------------------------------\n";
    // cria pasta "output" se não existir
    std::filesystem::create_directory("output");

    const std::string resultadosPath = "output/resultados.dat";
    const std::string outputPath = "output/output.dat";

    auto fileNeedsHeader = [](const std::string &path) {
        return !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    };

    std::ofstream resultados(resultadosPath, std::ios::app);
    if (resultados.is_open())
    {
        if (fileNeedsHeader(resultadosPath)) {
            resultados << "=== Resultados de Execução ===\n";
        }
        resultados << "\n[Processo PID " << pcb.pid << "] " << pcb.name << "\n";
        resultados << "Quantum: " << pcb.quantum << " | Timestamp: " << pcb.timeStamp << " | Prioridade: " << pcb.priority << "\n";
        resultados << "Ciclos de Pipeline: " << pcb.pipeline_cycles << "\n";
        resultados << "Ciclos de Memória: " << pcb.memory_cycles << "\n";
        resultados << "Acessos a Cache L1:     " << pcb.cache_mem_accesses.load() << "\n";
        resultados << "  - Reads:     " << pcb.cache_read_accesses.load() << "\n";
        resultados << "     - Hits:    " << pcb.cache_read_hits.load() << "\n";
        resultados << "     - Misses:    " << pcb.cache_read_misses.load() << "\n";
        resultados << "  - Writes:    " << pcb.cache_write_accesses.load() << "\n";
        resultados << "     - Hits:    " << pcb.cache_write_hits.load() << "\n";
        resultados << "     - Misses:    " << pcb.cache_write_misses.load() << "\n";
        resultados << "Ciclos de IO: " << pcb.io_cycles << "\n";
        resultados << "Tempo Total de Execução: " << pcb.totalTimeExecution() << "\n";
        resultados << "Cores Utilizados: ";
        for (const auto& core : pcb.coresAssigned) {
            resultados << core << " ";
        }
        resultados << "\n";
        resultados << "Saída do Programa (PID " << pcb.pid << "):\n";
        if (programOutput.empty()) {
            resultados << "  (Sem saída registrada)\n";
        } else {
            for (const auto &line : programOutput) {
                resultados << "  -> " << line << "\n";
            }
        }
        resultados << "------------------------------------------\n";
    }

    std::ofstream output(outputPath, std::ios::app);
    if (output.is_open())
    {
        if (fileNeedsHeader(outputPath)) {
            output << "=== Saída Lógica do Programa ===\n";
        }
        output << "\n[Programa: " << pcb.name << " | PID " << pcb.pid << "]\n";
        output << "Saída declarada (PID " << pcb.pid << "):\n";
        if (programOutput.empty()) {
            output << "  (Sem saída registrada)\n";
        } else {
            for (const auto &line : programOutput) {
                output << "  -> " << line << "\n";
            }
        }

        output << "\nRegistradores principais:\n";
        output << pcb.regBank.get_registers_as_string() << "\n";

        output << "\n=== Operações Executadas ===\n";
        std::string temp_filename = "output/temp_1.log";
        if (std::filesystem::exists(temp_filename))
        {
            std::ifstream temp_file(temp_filename);
            if (temp_file.is_open())
            {
                std::string line;
                while (std::getline(temp_file, line))
                {
                    output << line << "\n";
                }
                temp_file.close();
            }
            std::filesystem::remove(temp_filename);
        }
        else
        {
            output << "(Nenhuma operação registrada)\n";
        }
        output << "\n=== Fim das Operações Registradas ===\n";
    }

    resultados.close();
    output.close();
}