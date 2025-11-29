# ==========================================
# CONFIGURAÇÕES DO COMPILADOR
# ==========================================
CXX := g++
# Adicionei -pthread caso use threads no futuro (comum em SO)

# ==========================================
# NOMES DOS EXECUTÁVEIS
# ==========================================
TARGET := simulador
TARGET_HASH := test_hash_register
TARGET_BANK := test_register_bank

# ==========================================
# DEFINIÇÃO DOS ARQUIVOS FONTE (SOURCES)
# ==========================================

# 1. Arquivo principal
MAIN_SRC := src/main.cpp

# 2. Módulos do sistema (Pega todos os .cpp dentro destas pastas)
# Isso garante que IO, Memory, CPU, Parser e o ProcessScaler sejam compilados
MODULE_SRCS := $(wildcard src/cpu/*.cpp) \
               $(wildcard src/memory/*.cpp) \
               $(wildcard src/IO/*.cpp) \
               $(wildcard src/parser_json/*.cpp) \
               $(wildcard src/process_scaler/*.cpp) \
               $(wildcard src/tasks/*.cpp)

# Junta o main com os módulos
SRC := $(MAIN_SRC) $(MODULE_SRCS)
OBJ := $(SRC:.cpp=.o)

# Fontes específicos para testes isolados (não entram no build principal)
SRC_HASH := src/test_hash_register.cpp
OBJ_HASH := $(SRC_HASH:.cpp=.o)

SRC_BANK := src/test_register_bank.cpp src/cpu/REGISTER_BANK.cpp
OBJ_BANK := $(SRC_BANK:.cpp=.o)

# ==========================================
# REGRAS DE COMPILAÇÃO
# ==========================================

# Make clean -> make -> make run
all: clean $(TARGET) run

# Regra para o programa principal
$(TARGET): $(OBJ)
	@echo "🔨 Linkando o executável principal..."
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

# Regra para o teste do hash register
$(TARGET_HASH): $(OBJ_HASH)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ_HASH)

# Regra para o teste do register bank
$(TARGET_BANK): $(OBJ_BANK)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ_BANK)

# Regra genérica para transformar .cpp em .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ==========================================
# COMANDOS UTILITÁRIOS
# ==========================================

clean:
	@echo "🧹 Limpando arquivos antigos (.o e executáveis)..."
	@rm -f $(OBJ) $(OBJ_HASH) $(OBJ_BANK) $(TARGET) $(TARGET_HASH) $(TARGET_BANK)

run:
	@echo "🚀 Executando o Simuador..."
	@./$(TARGET)

# Testes Específicos
test-hash: clean $(TARGET_HASH)
	@echo "🧪 Executando teste do Hash Register..."
	@./$(TARGET_HASH)

test-bank: clean $(TARGET_BANK)
	@echo "🧪 Executando teste do Register Bank..."
	@./$(TARGET_BANK)

# Ajuda
help:
	@echo "📋 SO-SimuladorVonNeumann - Comandos:"
	@echo "  make          - Compila e roda o main.cpp (Simulador Completo)"
	@echo "  make clean    - Limpa arquivos compilados"
	@echo "  make test-hash - Roda teste de Hash"
	@echo "  make test-bank - Roda teste de Banco de Registradores"

.PHONY: all clean run test-hash test-bank help