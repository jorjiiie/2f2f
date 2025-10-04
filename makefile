# rzhu: thanks claude!

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -O2 -Werror

# Directories
SRC_DIR = src
BIN_DIR = bin

# Source files
SOURCES = $(SRC_DIR)/main.cc
TEST_SOURCES = $(SRC_DIR)/test.t.cc

# Output executables
TARGET = ${BIN_DIR}/main
TEST_TARGET = ${BIN_DIR}/test

# Default target
all: $(TARGET)

# Build main program
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Build and run tests
test: $(TEST_SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $^
	./$(TEST_TARGET)

# Just build tests without running
build-test: $(TEST_SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $^

# Run existing test executable
run-test: $(TEST_TARGET)
	./$(TEST_TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(TEST_TARGET) $(SRC_DIR)/a.out

# Phony targets
.PHONY: all clean test build-test run-test
