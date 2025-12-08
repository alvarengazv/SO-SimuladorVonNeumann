#include <exception>
#include <iostream>

#include "simulator/simulator.hpp"

int main() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif

    std::cout << "I------------------------------------------------I\n";
    std::cout << "I--- Simulador de Arquitetura de Von Neumann  ---I\n";
    std::cout << "I------------------------------------------------I\n";
    
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