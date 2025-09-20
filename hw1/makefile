CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc
CXXFLAGS = -Wall -Wextra -std=c++17 -g -Isrc
GTEST_FLAGS = -lgtest -lgtest_main -pthread

SRC_DIR = src
TEST_DIR = test
BUILD_DIR = build

# Targets
TEST_TARGET = $(BUILD_DIR)/test_b_tree
DEMO_TARGET = $(BUILD_DIR)/demo

SRC_FILES = $(SRC_DIR)/b_tree.c
DEMO_FILES = $(SRC_DIR)/demo.c
TEST_FILES = $(TEST_DIR)/test.cpp

OBJS = $(SRC_FILES:.c=.o)
DEMO_OBJS = $(DEMO_FILES:.c=.o)
TEST_OBJS = $(TEST_FILES:.cpp=.o)

$(shell mkdir -p $(BUILD_DIR))

.PHONY: all test demo clean

all: test demo

$(TEST_TARGET): $(OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GTEST_FLAGS)

$(DEMO_TARGET): $(OBJS) $(DEMO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	./$(TEST_TARGET)

demo: $(DEMO_TARGET)
	./$(DEMO_TARGET)

clean:
	rm -rf $(BUILD_DIR) *.o *.dot *.png test_tree.bin demo_tree.bin
	rm -f $(SRC_DIR)/*.o $(TEST_DIR)/*.o
