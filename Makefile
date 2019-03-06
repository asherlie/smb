CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic -lpthread
all: smb
smb: smb.c host.c client.c shared.c
clean:
	rm -f smb
