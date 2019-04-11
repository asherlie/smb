CC= gcc
CFLAGS= -Wall -Wextra -Wpedantic -lpthread -O3
CSRC = smb.c host.c client.c shared.c ash_table.c
all: smb
smb: $(CSRC)
db: $(CSRC)
	$(CC) $(CFLAGS) $(CSRC) -DASH_DEBUG -g -o db

rm:
	rm *.smbr /var/tmp/*.smbr /tmp/*.smbr

clean:
	rm -f smb db
