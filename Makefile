CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lX11
TARGET = filedrag
SRC = filedrag.c
INSTALL_DIR = /usr/local/bin 

all: compile

test: 
	./$(TARGET) filedrag.c

compile: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

debug: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS) -DDEBUG
	./$(TARGET) filedrag.c

install: all
	sudo cp $(TARGET) $(INSTALL_DIR)

clean:
	rm -f $(TARGET)
