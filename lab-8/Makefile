# ============================================================================
# Build Subsystem: MVCC + 2PL Financial Ledger Engine
# Target Standard: C++17
# ============================================================================

CXX      := c++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Wpedantic -Wshadow

TARGET   := asset_ledger_engine
OBJECTS  := main.o transaction_engine.o

.PHONY: all default run clean

all: $(TARGET)

default: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

main.o: main.cpp transaction_engine.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o $@

transaction_engine.o: transaction_engine.cpp transaction_engine.h
	$(CXX) $(CXXFLAGS) -c transaction_engine.cpp -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)