# This makefile must be included externally
APP_SRC =	app/main

.PHONY: raft-app
raft-app: raft-static $(APP_SRC)

.PHONY: clean-app
clean-app:
	rm -f app/*.co
	rm app/main.sky
