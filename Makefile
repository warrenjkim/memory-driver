CC = g++ -std=c++11
CFLAGS = -Wall -c
TARGET = memory_driver
SRC = *.cpp

# linker flags
LDFLAGS = -Wall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(LDFLAGS) -o $(TARGET) $(SRC)

# Clean target to remove executable
clean:
	rm -f $(TARGET) .*~ *~

