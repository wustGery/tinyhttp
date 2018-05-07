all: tinyhttpd client
LIBS = -pthread #-lsocket
tinyhttpd: tinyhttpd.c
	gcc -g -W -Wall $(LIBS) -o $@ $<

client: simpleclient.c
	gcc -W -Wall -o $@ $<
clean:
	rm tinyhttpd
