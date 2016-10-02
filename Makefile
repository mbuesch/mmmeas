# The toolchain definitions
CC		= gcc
INSTALL		= install
SPARSE		= sparse

V		= @		# Verbose build:  make V=1
C		= 0		# Sparsechecker build:  make C=1
Q		= $(V:1=)
QUIET_CC	= $(Q:@=@echo '     CC       '$@;)$(CC)
QUIET_DEPEND	= $(Q:@=@echo '     DEPEND   '$@;)$(CC)
ifeq ($(C),1)
QUIET_SPARSE	= $(Q:@=@echo '     SPARSE   '$@;)$(SPARSE)
else
QUIET_SPARSE	= @/bin/true
endif

PREFIX		?= /usr/local
CFLAGS		= -O2 -Wall -std=c99 -D_GNU_SOURCE -pedantic
LDFLAGS		=
SPARSEFLAGS	= $(CFLAGS) -D__transparent_union__=__unused__ -D_STRING_ARCH_unaligned=1 \
		  -D__DBL_MAX__=0.0l \
		  -Wdeclaration-after-statement -Wdo-while -Wptr-subtraction-blows \
		  -Wreturn-void -Wshadow -Wtypesign -Wundef

SRCS		= main.c es51984.c
BIN		= mmmeas

.SUFFIXES:
.PHONY: all install clean distclean
.DEFAULT_GOAL := all

DEPS = $(sort $(patsubst %.c,dep/%.d,$(1)))
OBJS = $(sort $(patsubst %.c,obj/%.o,$(1)))

# Generate dependencies
$(call DEPS,$(SRCS)): dep/%.d: %.c 
	@mkdir -p $(dir $@)
	$(QUIET_DEPEND) -o $@.tmp -MM -MT "$@ $(patsubst dep/%.d,obj/%.o,$@)" $(CFLAGS) $< && mv -f $@.tmp $@

-include $(call DEPS,$(SRCS))

# Generate object files
$(call OBJS,$(SRCS)): obj/%.o:
	@mkdir -p $(dir $@)
	$(QUIET_SPARSE) $(SPARSEFLAGS) $<
	$(QUIET_CC) -o $@ -c $(CFLAGS) $<

all: $(BIN)

$(BIN): $(call OBJS,$(SRCS))
	$(QUIET_CC) $(CFLAGS) -o $(BIN) $(call OBJS,$(SRCS)) $(LDFLAGS)

install: all
	$(INSTALL) -g 0 -o 0 -m 0755 -t $(PREFIX)/bin/ $(BIN)

clean:
	-rm -Rf *~ obj dep

distclean: clean
	-rm -f $(BIN)
