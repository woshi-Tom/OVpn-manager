CC = gcc
CFLAGS = -Wall -Wextra -I/usr/include/postgresql -g -I./include -D_GNU_SOURCE
LDFLAGS = -lyaml -lpq -lpthread -lcjson -lssl -lcrypto
TARGET = vpn_core
SRCS = main.c logger.c config.c database.c openvpn.c socket_server.c monitor.c cert_utils.c client_handler.c ca_handler.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all cleany