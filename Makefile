CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -Iinclude -Ithird_party
LDFLAGS  := -pthread

SRCS := src/main.cpp \
        src/DomainImpl.cpp \
        src/ConsoleAndTests.cpp \
        src/repository/MealWalletService.cpp \
        src/api/ApiServer.cpp

TARGET := mealwallet

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

test: $(TARGET)
	./$(TARGET) --test

console: $(TARGET)
	./$(TARGET) --console

seed: $(TARGET)
	./$(TARGET) --seed-demo

clean:
	rm -f $(TARGET)
	rm -rf build

.PHONY: all test console seed clean
