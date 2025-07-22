# ——— Compiler settings ———
CXX       := g++
CXXFLAGS  := -std=c++98 -Wall -Wextra -Wshadow -Werror \
             -Iinclude -g
LDFLAGS   :=

# ——— Directories ———
SRC_DIR   := src
OBJ_DIR   := build/obj
BIN_DIR   := build

# ——— Source files ———
SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
OBJS      := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# ——— Final target ———
TARGET    := $(BIN_DIR)/webserv

# ——— Default rule ———
.PHONY: all
all: $(TARGET)

# ——— Link step ———
$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking $@"
	$(CXX) $(LDFLAGS) -o $@ $^

# ——— Compile step ———
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@echo "Compiling $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ——— Create build dirs ———
$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

# ——— Convenience run target ———
# Example: make run ARGS="config/webserv.conf"
.PHONY: run
run: all
	@echo "Running $(TARGET) $(ARGS)"
	$(TARGET) $(ARGS)

# ——— Clean up ———
.PHONY: clean
clean:
	@echo "Cleaning build artifacts"
	@rm -rf $(OBJ_DIR)/*.o $(TARGET)

