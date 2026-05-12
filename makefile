# Variáveis de Compilação
CC       := gcc
CFLAGS   := -Wall -Wextra -Werror -std=c11 -O3
INCLUDES := -Iinclude
SRC_DIR  := src
OBJ_DIR  := obj
BIN      := miniquery

# Localizar todos os ficheiros .c na pasta src
SRCS     := $(wildcard $(SRC_DIR)/*.c)
# Gerar nomes de ficheiros .o na pasta obj
OBJS     := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Alvo principal (Default)
all: $(BIN)

# Linkagem do executável
$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

# Compilação dos objetos
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Criar pasta de objetos se não existir
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Alvo de testes (conforme exigido no documento)
test: $(BIN)
	@echo "A executar testes..."
	@# Aqui chamarias o teu script de teste ou binário de teste
	./$(BIN) --test 

# Limpeza do projeto
clean:
	rm -rf $(OBJ_DIR) $(BIN)
	@echo "Projeto limpo."

.PHONY: all clean test