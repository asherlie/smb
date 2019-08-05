CC= gcc
CFLAGS= -Wall -Wextra -Wpedantic -lpthread $(OPT)
CSRC = smb.c host.c client.c shared.c ash_table.c ashio.c
all: smb
smb: $(CSRC)
db: $(CSRC)
	$(CC) $(CFLAGS) $(CSRC) -DASH_DEBUG -g -O3 -o db

rm:
	rm *.smbr /var/tmp/*.smbr /tmp/*.smbr

clean:
	rm -f smb db
