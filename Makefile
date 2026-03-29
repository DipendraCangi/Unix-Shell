CC      = gcc
CFLAGS  = -Wall -Wextra
LIBS    = -lreadline
TARGET  = shell

all: $(TARGET)

$(TARGET): shell.c
	$(CC) $(CFLAGS) shell.c -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
