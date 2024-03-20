CFLAGS= -Wall -pedantic -std=gnu99

all: rolexhound

rolexhound:
	gcc $(CFLAGS) `pkg-config --cflags --libs libnotify` rolexhound.c -o build/rolexhoundd
