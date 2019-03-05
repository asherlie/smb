CC=gcc
CFLAGS=-Wall -Werror -Wpedantic
all: smb
smb: smb.c
clean:
	rm -f smb
