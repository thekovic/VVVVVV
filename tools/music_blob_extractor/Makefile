CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS ?=

OUT_DIR := build

TARGET := $(OUT_DIR)/music_blob_extractor
SRC = main.cpp

all: $(TARGET)

$(OUT_DIR):
	mkdir -p "$@"

$(TARGET): $(SRC) | $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o "$@" $(SRC)

clean:
	rm -rf $(OUT_DIR)

.PHONY: all clean
