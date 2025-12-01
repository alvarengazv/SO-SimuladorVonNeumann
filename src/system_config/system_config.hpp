#ifndef SYSTEM_CONFIG_HPP
#define SYSTEM_CONFIG_HPP

#include <string>
#include <iostream>
#include <fstream>
#include "../nlohmann/json.hpp" 

using json = nlohmann::json;

struct MainMemoryConfig {
    int total;
    int page_size;
    int weight;
};

struct SecondaryMemoryConfig {
    int total;
    int block_size;
    int weight;
};

struct CacheConfig {
    int size;
    int line_size;
    int weight;
    int policy;
};

struct CpuConfig {
    int cores;
};

struct SchedulingConfig {
    int algorithm;  
};

class SystemConfig {
public:
    MainMemoryConfig main_memory;
    SecondaryMemoryConfig secondary_memory;
    CacheConfig cache;
    CpuConfig cpu;
    SchedulingConfig scheduling;

    static SystemConfig loadFromFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Erro: Não foi possível abrir o arquivo de configuração: " + filePath);
        }

        json j;
        file >> j;

        SystemConfig config;

        config.main_memory.total = j.at("main_memory").at("total").get<int>();
        config.main_memory.page_size = j.at("main_memory").at("page_size").get<int>();
        config.main_memory.weight = j.at("main_memory").at("weight").get<int>();

        config.secondary_memory.total = j.at("secondary_memory").at("total").get<int>();
        config.secondary_memory.block_size = j.at("secondary_memory").at("block_size").get<int>();
        config.secondary_memory.weight = j.at("secondary_memory").at("weight").get<int>();

        config.cache.size = j.at("cache").at("size").get<int>();
        config.cache.line_size = j.at("cache").at("line_size").get<int>();
        config.cache.weight = j.at("cache").at("weight").get<int>();
        config.cache.policy = j.at("cache").at("policy").get<int>();

        config.cpu.cores = j.at("cpu").at("cores").get<int>();

        config.scheduling.algorithm = j.at("scheduling").at("algorithm").get<int>();

        return config;
    }
};

#endif