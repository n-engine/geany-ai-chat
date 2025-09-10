# Makefile pour ai_chat.so
## libgtksourceview-3.0-dev
CC = gcc
CFLAGS = -shared -fPIC -Wall -Wextra -O0 -ggdb
LIBS = -lcurl -ljson-c -lgthread-2.0
PKG_CFLAGS = $(shell pkg-config --cflags geany gtk+-3.0 gtksourceview-3.0)
PKG_LIBS = $(shell pkg-config --libs geany gtk+-3.0 gtksourceview-3.0) \
           -lcurl -lgthread-2.0 \
           -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now

CFLAGS += -fstack-protector-strong -D_FORTIFY_SOURCE=3

all: ai_chat.so

ai_chat.so: ai_chat.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ $< $(PKG_LIBS) $(LIBS)

install:
	mkdir -p $(HOME)/.config/geany/plugins
	sudo cp ai_chat.so /usr/local/lib/geany/

clean:
	$(RM) ai_chat.so
