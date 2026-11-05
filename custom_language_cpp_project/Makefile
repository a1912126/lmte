CXX := g++
LLVM_CONFIG ?= $(shell command -v llvm-config 2>/dev/null || { prefix=$$(brew --prefix llvm 2>/dev/null); test -n "$$prefix" && printf '%s/bin/llvm-config' "$$prefix"; })
LLVM_INCLUDEDIR := $(shell $(LLVM_CONFIG) --includedir)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS := $(shell $(LLVM_CONFIG) --libs core passes support)
LLVM_SYSTEM_LIBS := $(shell $(LLVM_CONFIG) --system-libs)
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude -isystem $(LLVM_INCLUDEDIR)

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
TARGET := customlang

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LLVM_LDFLAGS) $(LLVM_LIBS) $(LLVM_SYSTEM_LIBS) -o $@

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) demo.psk

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all run clean
