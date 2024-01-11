CC=gcc
CFLAGS=-pthread -Wall

apagon: apagon.c
	$(CC) $(CFLAGS) apagon.c -o apagon
