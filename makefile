CC= gcc
CFLAGS= -Wall -Wextra -Wpedantic -lpthread
CSRC = smb.c host.c client.c shared.c
all: smb
smb: $(CSRC)
db: $(CSRC)
	$(CC) $(CFLAGS) $(CSRC) -DASH_DEBUG -g -o db

rm:
	rm /var/tmp/*.smbr

clean:
	rm -f smb db
