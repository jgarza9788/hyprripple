# HyprRipple native Hyprland plugin

# pipefail so `make test` fails when a test binary fails, not when tail does
SHELL       := /bin/bash
.SHELLFLAGS := -o pipefail -c

CXX      ?= g++
CXXFLAGS  = -fPIC -g -O2 -std=c++23 -Wall -Wno-deprecated-declarations
LDFLAGS   = -shared
INCLUDES  = $(shell pkg-config --cflags hyprland pixman-1 libdrm) -Inative/build
LIBS      = $(shell pkg-config --libs hyprland)

ifeq ($(basename $(CXX)),g++)
	CXXFLAGS += --no-gnu-unique
endif

TARGET   = hyprripple.so
SOURCES  = $(wildcard native/src/*.cpp)
OBJ      = $(patsubst native/src/%.cpp,native/build/%.o,$(SOURCES))
HEADERS  = $(wildcard native/src/*.hpp) native/build/ripple.frag.inc

all: $(TARGET)

# Embed the GLSL as a C++ raw string literal.
native/build/ripple.frag.inc: native/shaders/ripple.frag
	@mkdir -p native/build
	@{ printf 'R"GLSL('; cat $<; printf ')GLSL"\n'; } > $@

native/build/%.o: native/src/%.cpp $(HEADERS)
	@mkdir -p native/build
	@echo "[$(CXX)] $<"
	@$(CXX) -c $(CXXFLAGS) $(INCLUDES) $< -o $@

$(TARGET): $(OBJ)
	@$(CXX) $(LDFLAGS) $(OBJ) -o $@ $(LIBS)
	@echo "Built $@"

# Pure-logic unit tests (no Hyprland needed), under ASan + UBSan, plus the
# panel model tests.
test: native/build/logic_test
	@native/build/logic_test | grep -v '^PASS'
	@node tests/model.test.js | grep -v '^PASS'

native/build/logic_test: native/src/Logic.cpp native/src/Logic.hpp native/tests/logic_test.cpp
	@mkdir -p native/build
	@$(CXX) -std=c++23 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all native/src/Logic.cpp native/tests/logic_test.cpp -o $@

clean:
	rm -rf native/build $(TARGET)

.PHONY: all clean test
