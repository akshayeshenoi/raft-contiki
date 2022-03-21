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
TARGET_LIBFILES = -L. -lraft

all: raft-static raft-app

.PHONY: raft-static
raft-static: libraft.a

libraft.a: $(RAFT_OBJS) contiki-$(TARGET).a
	$(TRACE_AR)
	$(Q)$(AR) -rcs libraft.a $(RAFT_OBJS) $(CONTIKI_OBJECTFILES)

clean-raft:
	if ls src/*.co 1> /dev/null 2>&1; then rm src/*.co; fi;
	if [ -f libraft.a ]; then rm libraft.a; fi;
	if [ -f symbols.h ]; then rm symbols.c symbols.h; fi;
	$(MAKE) clean-app
	$(MAKE) clean

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
