PROG ?= iot-ubusd
DEFS ?= -liot-base-nossl -liot-json -lubus -lubox -lblobmsg_json -lpthread
EXTRA_CFLAGS ?= -Wall -Werror
CFLAGS += $(DEFS) $(EXTRA_CFLAGS)

SRCS = main.c ubusd.c mqtt.c

all: $(PROG)

$(PROG):
	$(CC) $(SRCS) $(CFLAGS) -o $@


clean:
	rm -rf $(PROG) *.o
