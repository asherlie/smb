CC=gcc
CFLAGS= -Wall -Wextra -Wpedantic -pthread
OBJ=ash_table.o ashio.o shared.o client.o host.o

all: smb

ash_table.o: ash_table.c ash_table.h
	$(CC) $(CFLAGS) ash_table.c -c

ashio.o: ashio.c ashio.h
	$(CC) $(CFLAGS) ashio.c -c

shared.o: shared.h shared.c
	$(CC) $(CFLAGS) shared.c -c

client.o: ash_table.o shared.o client.c client.h
	$(CC) $(CFLAGS) client.c -c

host.o: ash_table.o shared.o host.c host.h
	$(CC) $(CFLAGS) host.c -c

smb: $(OBJ) smb.c
	$(CC) $(CFLAGS) $(OBJ) smb.c -o smb

# used for debugging
db: $(CSRC)
	$(CC) $(CFLAGS) $(CSRC) -DASH_DEBUG -g -O3 -o db

# phony targets
.PHONY:
clean:
	rm -f $(OBJ) smb db

.PHONY:
rm:
	rm *.smbr /var/tmp/*.smbr /tmp/*.smbr
