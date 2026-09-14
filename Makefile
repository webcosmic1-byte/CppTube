# CppTube — build with any C++17 compiler
#   Linux / macOS : make
#   Windows      : make CXX=g++  (MinGW)   or  see README for MSVC

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
# Statically link the C++ runtime so the binary runs on a slim base image
# (e.g. debian:bookworm-slim inside the Docker/Render image) without
# needing a matching libstdc++ version.
LDFLAGS  ?= -pthread -static-libstdc++ -static-libgcc

all: cpptube

cpptube: server.cpp include/httplib.h include/json.hpp
	$(CXX) $(CXXFLAGS) -Iinclude -o $@ server.cpp $(LDFLAGS)

clean:
	rm -f cpptube

.PHONY: all clean
