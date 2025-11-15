CXX ?= x86_64-w64-mingw32-g++
CXXFLAGS += -std=c++20 -O2 -Wall -Wextra -Wpedantic -municode -Iinclude
LDFLAGS += -mwindows -lgdi32 -lcomctl32 -luxtheme -lmsimg32

SRC := src/main.cpp \
        src/cash_sloth_json.cpp \
        src/cash_sloth_style.cpp

all: cash-sloth.exe

cash-sloth.exe: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f cash-sloth.exe

.PHONY: all clean
