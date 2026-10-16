all: build/server build/client

build/server: src/server.cpp src/common.cpp src/common.h | build
	g++ src/server.cpp src/common.cpp -o build/server
	
build/client : src/client.cpp src/common.cpp src/common.h | build
	g++ src/client.cpp src/common.cpp -o build/client

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean