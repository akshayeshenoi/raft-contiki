CONTIKI = ./deps/contiki

ifndef TARGET
TARGET=sky
endif

include app/Makefile

SHELL  = /bin/bash
CFLAGS += -Iinclude

RAFT_SRC =	src/raft_server.c \
			src/raft_server_properties.c \
			src/raft_node.c \
			src/raft_log.c

RAFT_OBJS=$(RAFT_SRC:.c=.co)
RAFT_LIB += -L. -lraft

all: raft-static raft-app

.PHONY: raft-static
raft-static: libraft.a

libraft.a: $(RAFT_OBJS) contiki-$(TARGET).a
	$(AR) -rcs libraft.a $(RAFT_OBJS)

clean-raft:
	if ls src/*.co 1> /dev/null 2>&1; then rm src/*.co; fi;
	if [ -f libraft.a ]; then rm libraft.a; fi;
	if [ -f symbols.h ]; then rm symbols.c symbols.h; fi;
	$(MAKE) clean-app
	$(MAKE) clean

## override contiki's link recipe
# we do this so we can add contiki-$(TARGET).a as a dependency to our libraft.a
CUSTOM_RULE_LINK=1
%.$(TARGET): %.co $(PROJECT_OBJECTFILES) $(PROJECT_LIBRARIES) contiki-$(TARGET).a
	$(TRACE_LD)
	$(Q)$(LD) $(LDFLAGS) $(TARGET_STARTFILES) $(RAFT_LIB) ${filter-out %.a,$^} \
	    ${filter %.a,$^} $(TARGET_LIBFILES) -o $@


CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
