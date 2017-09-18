PWD=$(shell pwd)
BIN=$(PWD)/bin

CC=gcc
CFLAGS= -Wall -Werror
DEBUG=

.PHONY: all do_env_check server client clean

all: server client

do_env_check:
	@$(PWD)/env_check.sh
	@echo "Env checked ok."

server: virt-server.c
	$(CC) $(DEBUG) virt-server.c $(CFLAGS) -o $(BIN)/virt-server

client: virt-client.c
	$(CC) $(DEBUG) virt-client.c $(CFLAGS) -o $(BIN)/virt-client

clean:
	-rm -rf $(BIN)/*
