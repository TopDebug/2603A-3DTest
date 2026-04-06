.PHONY: all configure build rebuild clean run shaders

CONFIG_PRESET=default
BUILD_PRESET=default
TARGET=Test

all: build

configure:
	cmake --preset $(CONFIG_PRESET)

build: configure
	cmake --build --preset $(BUILD_PRESET) --target $(TARGET) -- /m:1

rebuild: clean build

clean:
	cmake --build --preset $(BUILD_PRESET) --target clean -- /m:1

run: build
	"D:/DEVELOP/build/Test/Debug/Test.exe"

shaders:
	glslc -g -O0 shaders/shader.vert -o shaders/shader.vert.spv
	glslc -g -O0 shaders/shader.frag -o shaders/shader.frag.spv
