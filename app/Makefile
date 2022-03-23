# This makefile must be included externally
APP_SRC =	app/main

.PHONY: raft-app
raft-app: $(APP_SRC) 

# adding this dependency to trigger recompile if libraft changes
$(APP_SRC).$(TARGET): libraft.a

.PHONY: clean-app
clean-app:
	rm -f app/*.co
	rm app/main.sky
