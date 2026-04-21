# Compiler and Flags
CC = gcc
CFLAGS = -W -Wall -O2
# Linking the Winsock2 library for Windows networking
LIBS = -lws2_32

# Output executable
TARGET = main.exe

# Source and Object files
SRCS = main.c mongoose.c
OBJS = $(SRCS:.c=.o)

# Default build target
all: $(TARGET)

# Linking step
$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LIBS)

# Compilation step (depends on mongoose.h)
%.o: %.c mongoose.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean command for Windows (cmd)
clean:
	del /f $(TARGET) $(OBJS)

Debug: all
