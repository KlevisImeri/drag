CC = gcc
CFLAGS = -Wall -Wextra
INCLUDE_DIR = include
CFLAGS += -I$(INCLUDE_DIR)
TARGET = drag
SRC = drag.c
INSTALL_DIR = /usr/local/bin

SESSION_TYPE := $(shell echo $$XDG_SESSION_TYPE)

ifeq ($(SESSION_TYPE),wayland)
    LIBS = -lwayland-client -lrt
    CFLAGS += -D_WAYLAND_SESSION
    SRC += xdg-shell-client-protocol.c
    $(info Detected Wayland session.)
else
    LIBS = -lX11
    CFLAGS += -D_X11_SESSION
    $(info Detected X11 session (or unknown).)
endif

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
