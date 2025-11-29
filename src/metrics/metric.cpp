#include "metrics.hpp"

void print_metrics(const PCB &pcb) {
    std::cout << "\n--- METRICAS FINAIS DO PROCESSO " << pcb.pid << " ---\n";
    std::cout << "Nome do Processo:       " << pcb.name << "\n";
    std::cout << "Estado Final:           " << (pcb.state == State::Finished ? "Finished" : "Incomplete") << "\n";
    std::cout << "Ciclos de Pipeline:     " << pcb.pipeline_cycles.load() << "\n";
    std::cout << "Total de Acessos a Mem: " << pcb.mem_accesses_total.load() << "\n";
    std::cout << "  - Leituras:             " << pcb.mem_reads.load() << "\n";
    std::cout << "  - Escritas:             " << pcb.mem_writes.load() << "\n";
    std::cout << "Acessos a Cache L1:     " << pcb.cache_mem_accesses.load() << "\n";
    std::cout << "Acessos a Mem Principal:" << pcb.primary_mem_accesses.load() << "\n";
    std::cout << "Acessos a Mem Secundaria:" << pcb.secondary_mem_accesses.load() << "\n";
    std::cout << "Ciclos Totais de Memoria: " << pcb.memory_cycles.load() << "\n";
    std::cout << "------------------------------------------\n";
    // cria pasta "output" se não existir
    std::filesystem::create_directory("output");

    // abre arquivos
    std::ofstream resultados("output/resultados.dat");
    std::ofstream output("output/output.dat");

    if (resultados.is_open())
    {
        resultados << "=== Resultados de Execução ===\n";
        resultados << "PID: " << pcb.pid << "\n";
        resultados << "Nome: " << pcb.name << "\n";
        resultados << "Quantum: " << pcb.quantum << "\n";
        resultados << "Prioridade: " << pcb.priority << "\n";
        resultados << "Ciclos de Pipeline: " << pcb.pipeline_cycles << "\n";
        resultados << "Ciclos de Memória: " << pcb.memory_cycles << "\n";
        resultados << "Cache Hits: " << pcb.cache_hits << "\n";
        resultados << "Cache Misses: " << pcb.cache_misses << "\n";
        resultados << "Ciclos de IO: " << pcb.io_cycles << "\n";
    }

    if (output.is_open())
    {
        output << "=== Saída Lógica do Programa ===\n";

        // Dump de registradores
        output << "Registradores principais:\n";
        output << pcb.regBank.get_registers_as_string() << "\n";

        // Inserir operações registradas
        output << "\n=== Operações Executadas ===\n";

        // Lê o arquivo temporário com operações
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

            // Remove arquivo temporário após consolidar
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