SRCDIR := src
OBJDIR := obj

PKGCONFIG = $(shell which pkg-config)
CFLAGS := -g -Wall -Wno-unused-variable
#CFLAGS = -g -Wall -Wno-unused-variable $(shell $(PKGCONFIG) --cflags gtk4 --libs dbus-1 --libs libpulse)

LIBS   := -lasound -lcurl -lX11 -lmpdclient
CC := cc

# for gtk (gdebus interface to MPRIS)
#LDLIBS = $(shell pkg-config --libs gtk4)


$(shell mkdir -p $(OBJDIR))
NAME := $(shell basename $(shell pwd))

# recursive find of source files
SOURCES     := $(shell find $(SRCDIR) -type f -name *.c)

# create object files in separate directory
OBJECTS := $(SOURCES:%.c=$(OBJDIR)/%.o)

# debug
#$(info    SOURCES is: $(SOURCES))
#$(info    OBJECTS is: $(OBJECTS))

all: $(OBJECTS)
	@echo "== LINKING EXECUTABLE: $(NAME)"
	$(CC) $^ $(CFLAGS) $(LIBS) $(LDLIBS) -o $@ -o $(NAME)

$(OBJDIR)/%.o: %.c
	@echo "== COMPILING SOURCE $< --> OBJECT $@"
	@mkdir -p '$(@D)'
	$(CC) -I$(SRCDIR) $(CFLAGS) $(LIBS) $(LDLIBS) -c $< -o $@
