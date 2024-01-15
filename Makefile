CC=gcc
CFLAGS=-pthread -Wall

blackout: blackout.c
	$(CC) $(CFLAGS) blackout.c -o blackout
