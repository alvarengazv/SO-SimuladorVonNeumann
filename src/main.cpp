#include <exception>
#include <iostream>

#include "simulator/simulator.hpp"

int main() {
    try {
        Simulator simulator;
        return simulator.run();
    } catch (const std::exception &ex) {
        std::cerr << "Erro fatal: " << ex.what() << "\n";
    } catch (...) {
        std::cerr << "Erro desconhecido encontrado.\n";
    }

    return 1;
}