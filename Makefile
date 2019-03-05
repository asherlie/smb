CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic
all: smb
smb: smb.c
clean:
	rm -f smb
