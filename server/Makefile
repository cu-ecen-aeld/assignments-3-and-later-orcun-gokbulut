ALL_CFLAGS := -Wall -Werror -ggdb3 $(CFLAGS)
INSTALL_PROGRAM := $(INSTALL)
prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin

TARGET := aesdsocket
SOURCES := server.c
OBJECTS := $(SOURCES:.c=.o)
 
.DEFAULT: all

.PHONY: all
all: $(TARGET)

%.o: %.c Makefile
	@echo "[CC] $<"
	@$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -o $@ -c $<

$(TARGET): $(OBJECTS)
	@echo "[LD] $@"
	@$(CC) $(LDFLAGS) -o $@ $^

install: $(TARGET)
	@$(INSTALL_PROGRAM) $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)

.PHONY: clean
clean:
	@echo "[RM] $(TARGET)"
	@$(RM) $(TARGET)
	@$(foreach OBJECT,$(OBJECTS), \
		@echo "[RM] $(OBJECT)" ; \
		$(RM) $(OBJECT) \
  	)
