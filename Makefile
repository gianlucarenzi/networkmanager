# Compiler and flags
CC = gcc
CFLAGS = \
	-Iinc \
	$(shell pkg-config --cflags dbus-1)
	

LDFLAGS = \
	$(shell pkg-config --libs dbus-1)


# Source files
SRCS = \
	src/main.c \
	src/ethapi.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable name
TARGET = networkManager

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
