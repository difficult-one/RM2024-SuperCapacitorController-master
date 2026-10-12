CXX ?= g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Werror -O0 -g
TARGET := test_input_power_policy.exe
.PHONY: all test
all: $(TARGET)
$(TARGET): test_input_power_policy.cpp ../../Core/Inc/InputPowerPolicy.hpp
	$(CXX) $(CXXFLAGS) -I../../Core/Inc test_input_power_policy.cpp -o $(TARGET)
test: $(TARGET)
	./$(TARGET)
