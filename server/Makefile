ALL_CFLAGS := -Wall -Werror -ggdb3 $(CFLAGS)
DESTDIR ?= /
prefix ?= /usr/local
bindir ?= $(prefix)/bin

INSTALL := install

TARGET := aesdsocket
SOURCES := server.c
OBJECTS := $(SOURCES:.c=.o)
DEPENDENCIES:=$(SOURCES:.c=.d)

.DEFAULT: all
.PHONY: all
all: $(TARGET)

-include $(DEPENDENCIES)

%.o: %.c Makefile
	@echo "[CC] $<"
	@$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -MMD -MF $*.d -o $@ -c $<

$(TARGET): $(OBJECTS)
	@echo "[LD] $@"
	@$(CC) $(LDFLAGS) -o $@ $^

install: $(TARGET)
	@echo "[INSTALL] aesdsocket.service.sh"	
	@$(INSTALL) -T -D -m 0755 aesdsocket.service.sh $(DESTDIR)/etc/init.d/aesdsocket
	@echo "[INSTALL] $(TARGET)"
	@$(INSTALL) -T -D $(TARGET) $(DESTDIR)/$(bindir)/$(TARGET)
	@echo "[LN] S99aesdsocket"
	@mkdir -p $(DESTDIR)/etc/rcS.d
	@ln -s /etc/init.d/aesdsocket $(DESTDIR)/etc/rcS.d/S99aesdsocket

.PHONY: clean
clean:
	@echo "[RM] $(TARGET)"
	@$(RM) $(TARGET)
	@$(foreach OBJECT,$(OBJECTS), \
		@echo "[RM] $(OBJECT)" ; \
		$(RM) $(OBJECT) \
  	)
	@$(foreach DEPENDENCY,$(DEPENDENCIES), \
		@echo "[RM] $(DEPENDENCY)" ; \
		$(RM) $(DEPENDENCY) \
  	)
