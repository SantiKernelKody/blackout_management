CC=gcc
CFLAGS=-pthread

blackout: blackout.c
	$(CC) $(CFLAGS) blackout.c -o blackout
