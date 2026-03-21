CXX = g++
CXXFLAGS = -std=c++20 -O3 -pthread -Wall -I. $(shell pkg-config --cflags glfw3 2>/dev/null || echo "-I/usr/include")
LDFLAGS = -lvulkan -lpthread $(shell pkg-config --libs glfw3 2>/dev/null || echo "-lglfw")

SRC = main.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = bh_vulkan

all: shaders $(TARGET) sweep_bin

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

sweep_bin: sweep.cpp sweep.spv
	$(CXX) $(CXXFLAGS) -o $@ sweep.cpp $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

shaders:
	glslangValidator -V shader.vert -o vert.spv
	glslangValidator -V shader.frag -o frag.spv
	glslangValidator -V sweep.comp -o sweep.spv
	glslangValidator -V sph.comp -o sph.spv

clean:
	rm -f $(OBJ) $(TARGET) sweep_bin vert.spv frag.spv sweep.spv sph.spv *.ppm *.csv

.PHONY: all shaders clean
