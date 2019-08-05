# we're disabling optimizations for mac because ashio
# sometimes crashes during tab completion when compiled
# with optimizations
OS= $(shell uname)
ifeq ($(OS),Darwin)
OPT=
else
OPT=-O3
endif

CC= gcc
CFLAGS= -Wall -Wextra -Wpedantic -lpthread $(OPT)
CSRC = smb.c host.c client.c shared.c ash_table.c ashio.c
all: smb
smb: $(CSRC)
db: $(CSRC)
	$(CC) $(CFLAGS) $(CSRC) -DASH_DEBUG -g -o db

rm:
	rm *.smbr /var/tmp/*.smbr /tmp/*.smbr

clean:
	rm -f smb db
