APPNAME = timer

DEPDIR  := .d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td

CC	= gcc
CFLAGS	= -Wall -Wextra -Wdeclaration-after-statement -Wvla \
	  -g -O2 -Wp,-D_FORTIFY_SOURCE=2 --param=ssp-buffer-size=4 \
	  -fPIC -fexceptions -pipe \
	  -I../include \
	  $(shell pkg-config --cflags gtk+-3.0 glib-2.0 gmodule-2.0)
LDFLAGS = -Wl,-z,now,-z,defs,-z,relro,--as-needed -fpie
LIBS	= $(shell pkg-config --libs gtk+-3.0 glib-2.0 gmodule-2.0)
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

sources = $(wildcard *.c)
objects = $(sources:.c=.o)

ifeq ($(ASAN),1)
        override ASAN = -fsanitize=address
endif

v = @
ifeq ($V,1)
	v =
endif

.PHONY: all
all: $(APPNAME)

$(APPNAME): $(objects)
	@echo -e "  LNK\t$@"
	$(v)$(CC) $(LDFLAGS) $(ASAN) -o $@ $(objects) $(LIBS)

%.o: %.c
%.o: %.c $(DEPDIR)/%.d
	@echo -e "  CC\t$@"
	$(v)$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	$(POSTCOMPILE)

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(sources))))

.PHONY: clean
clean:
	$(v)rm -f $(objects) $(APPNAME)
	$(v)rm -f $(DEPDIR)/*
	$(v)rmdir $(DEPDIR)
