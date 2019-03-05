CC=gcc
CFLAGS="-o smb -Wall -Werror -Wpedantic"
all: smb
smb: smb.c
clean:
	rm -f smb
