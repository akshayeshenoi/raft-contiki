CONTIKI = ./deps/contiki

ifndef TARGET
TARGET=sky
endif

SHELL  = /bin/bash
CFLAGS += -Iinclude

RAFT_SRC =	src/raft_server.c \
			src/raft_server_properties.c \
			src/raft_node.c \
			src/raft_log.c

RAFT_OBJS=$(RAFT_SRC:.c=.o)

APP_SRC =	app/main
TARGET_LIBFILES += -L. -lraft

all: raft-static raft-app

.PHONY: raft-static
raft-static: $(RAFT_OBJS)
	$(AR) -rcs libraft.a $(RAFT_OBJS)

.PHONY: raft-app
raft-app: raft-static $(APP_SRC)

clean-raft:
	rm -f src/*.o; \
	if [ -f "libraft.so" ]; then rm libraft.so; fi;\
	if [ -f libraft.a ]; then rm libraft.a; fi;

CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
