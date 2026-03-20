CXX = g++
CXXFLAGS = -std=c++17 -O3 -pthread -Wall -I. $(shell pkg-config --cflags glfw3 2>/dev/null || echo "-I/usr/include")
LDFLAGS = -lvulkan -lpthread $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")

SRC = main.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = bh_vulkan

all: shaders $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

shaders:
	glslangValidator -V shader.vert -o vert.spv
	glslangValidator -V shader.frag -o frag.spv

clean:
	rm -f $(OBJ) $(TARGET) vert.spv frag.spv

.PHONY: all shaders clean
