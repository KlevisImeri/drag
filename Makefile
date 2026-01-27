CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lX11
TARGET = drag
SRC = drag.c
INSTALL_DIR = /usr/local/bin 

all: compile

test: compile 
	./$(TARGET) drag.c

compile: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

debug: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS) -DDEBUG
	./$(TARGET) drag.c

install: all
	sudo cp $(TARGET) $(INSTALL_DIR)

clean:
	rm -f $(TARGET)
