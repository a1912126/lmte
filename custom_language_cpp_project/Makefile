CXX := g++

# LLVM is only needed for the optional --llvm-ir output. If llvm-config cannot be
# found (or NO_LLVM=1 is given), the compiler is built without it and --llvm-ir
# reports that it is unavailable. Run 'make clean' after switching between the two.
LLVM_CONFIG ?= $(shell command -v llvm-config 2>/dev/null || { prefix=$$(brew --prefix llvm 2>/dev/null); test -n "$$prefix" && printf '%s/bin/llvm-config' "$$prefix"; })
ifneq ($(NO_LLVM),1)
LLVM_FOUND := $(shell test -n "$(LLVM_CONFIG)" && test -x "$(LLVM_CONFIG)" && echo yes)
endif

CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -pthread -Iinclude -MMD -MP

ifeq ($(LLVM_FOUND),yes)
LLVM_INCLUDEDIR := $(shell $(LLVM_CONFIG) --includedir)
CXXFLAGS += -isystem $(LLVM_INCLUDEDIR)
LDLIBS := $(shell $(LLVM_CONFIG) --ldflags) $(shell $(LLVM_CONFIG) --libs core passes support) $(shell $(LLVM_CONFIG) --system-libs)
else
CXXFLAGS += -DPLAINSPEAK_NO_LLVM
LDLIBS :=
ifneq ($(MAKECMDGOALS),clean)
$(info Note: LLVM not found - building without --llvm-ir support. Set LLVM_CONFIG=/path/to/llvm-config to enable it.)
endif
endif

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
DEPENDS := $(OBJECTS:.o=.d)
TARGET := customlang

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDLIBS) -o $@

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) demo.psk

test: $(TARGET)
	./test.sh

clean:
	rm -f $(OBJECTS) $(DEPENDS) $(TARGET)

-include $(DEPENDS)

.PHONY: all run test clean
