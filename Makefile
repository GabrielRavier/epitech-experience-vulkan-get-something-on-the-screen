override CXXFLAGS += -std=c++17 -Og
override LDFLAGS += -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

.PHONY: clean

all: vulkan-test shaders/vert.spv shaders/frag.spv

vulkan-test: src/main.cpp
	g++ -o vulkan-test src/main.cpp $(CXXFLAGS) $(LDFLAGS)

# We need to generate a spv file becauser that's what Vulkan actually reads
# Note: we could do the compilation within our code but that'd be incredibly elaborate compared to just doing this
shaders/vert.spv: shaders/shader.vert
	glslc shaders/shader.vert -o shaders/vert.spv

shaders/frag.spv: shaders/shader.frag
	glslc shaders/shader.frag -o shaders/frag.spv

clean:
	rm ./vulkan-test
