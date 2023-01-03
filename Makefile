override CXXFLAGS += -std=c++17 -O2
override LDFLAGS += -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

.PHONY: clean

all: vulkan-test

vulkan-test: src/main.cpp
	g++ -o vulkan-test src/main.cpp $(CXXFLAGS) $(LDFLAGS)

clean:
	rm ./vulkan-test
