CXX      ?= g++
STD      := -std=c++20
WARN     := -Wall -Wextra -Wpedantic -Werror
INC      := -Iinclude
REL      := -O3 -DNDEBUG -flto -fno-exceptions -fno-rtti
# Optional: make NATIVE=1 bench  -> -march=native
ifdef NATIVE
REL += -march=native
endif
BUILD    := build

all: $(BUILD)/silica-sim $(BUILD)/tests $(BUILD)/bench

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/tests: tests/test_main.cpp include/silica/*.hpp | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/bench: benches/bench_main.cpp include/silica/*.hpp | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/silica-sim: src/main.cpp include/silica/*.hpp | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) $(REL) $< -o $@
$(BUILD)/tests-san: tests/test_main.cpp include/silica/*.hpp | $(BUILD)
	$(CXX) $(STD) $(WARN) $(INC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $< -o $@

test: $(BUILD)/tests
	$(BUILD)/tests
san: $(BUILD)/tests-san
	$(BUILD)/tests-san
bench: $(BUILD)/bench
	$(BUILD)/bench
clean:
	rm -rf $(BUILD)

.PHONY: all test san bench clean
