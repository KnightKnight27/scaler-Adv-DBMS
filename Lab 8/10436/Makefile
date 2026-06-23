CXX = g++
CXXFLAGS = -std=c++17 -Wall
TARGET = txn_manager
SRCS = MVCCStore.cpp LockManager.cpp DeadlockDetector.cpp TransactionManager.cpp main.cpp

all: $(TARGET)

$(TARGET): $(SRCS) types.h MVCCStore.h LockManager.h DeadlockDetector.h TransactionManager.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
