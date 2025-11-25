# Makefile pour ai_chat.so (version modulaire)
## Dépendances: libgtksourceview-3.0-dev, libcurl4-openssl-dev

CC = gcc
CFLAGS = -fPIC -Wall -Wextra -O0 -ggdb -fstack-protector-strong -D_FORTIFY_SOURCE=3

PKG_CFLAGS = $(shell pkg-config --cflags geany gtk+-3.0 gtksourceview-3.0)
PKG_LIBS = $(shell pkg-config --libs geany gtk+-3.0 gtksourceview-3.0) \
           -lcurl -lgthread-2.0 \
           -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now

SRCDIR = src
OBJDIR = obj

SOURCES = $(SRCDIR)/ai_chat.c \
          $(SRCDIR)/prefs.c \
          $(SRCDIR)/history.c \
          $(SRCDIR)/network.c \
          $(SRCDIR)/models.c \
          $(SRCDIR)/ui_render.c \
          $(SRCDIR)/ui.c

OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGET = ai_chat.so

all: $(OBJDIR) $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET): $(OBJECTS)
	$(CC) -shared -o $@ $(OBJECTS) $(PKG_LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -I$(SRCDIR) -c $< -o $@

install:
	mkdir -p $(HOME)/.config/geany/plugins
	sudo cp $(TARGET) /usr/local/lib/geany/

clean:
	$(RM) -r $(OBJDIR) $(TARGET)

# Dependencies
$(OBJDIR)/ai_chat.o: $(SRCDIR)/prefs.h $(SRCDIR)/history.h $(SRCDIR)/network.h $(SRCDIR)/ui.h
$(OBJDIR)/prefs.o: $(SRCDIR)/prefs.h
$(OBJDIR)/history.o: $(SRCDIR)/history.h $(SRCDIR)/prefs.h
$(OBJDIR)/network.o: $(SRCDIR)/network.h $(SRCDIR)/history.h $(SRCDIR)/prefs.h
$(OBJDIR)/models.o: $(SRCDIR)/models.h $(SRCDIR)/prefs.h
$(OBJDIR)/ui_render.o: $(SRCDIR)/ui_render.h $(SRCDIR)/prefs.h
$(OBJDIR)/ui.o: $(SRCDIR)/ui.h $(SRCDIR)/prefs.h $(SRCDIR)/history.h $(SRCDIR)/network.h $(SRCDIR)/ui_render.h $(SRCDIR)/models.h

.PHONY: all clean install
