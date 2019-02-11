CC = gcc
CFLAGS  = -Wall -D_FILE_OFFSET_BITS=64
FUSE = `pkg-config fuse --cflags --libs`
MD5FLAGS = -lcrypto -lssl

default: net_server net_client

net_server: parser.c net_server.c 
	$(CC) $(CFLAGS) parser.c net_server.c $(FUSE) -o net_server.o $(MD5FLAGS)

net_client: parser.c log.c cash.c net_client.c
	$(CC) $(CFLAGS) parser.c log.c cash.c net_client.c $(FUSE) -o net_client.o $(MD5FLAGS)


clean: $(RM) default *.o *~





