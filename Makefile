PROG ?= iot-ubusd
DEFS ?= -liot-base-nossl -liot-json -llua -lubus -lubox -lblobmsg_json
EXTRA_CFLAGS ?= -Wall -Werror
CFLAGS += $(DEFS) $(EXTRA_CFLAGS)

SRCS = main.c ubusd.c

all: $(PROG)

$(PROG):
	$(CC) $(SRCS) $(CFLAGS) -o $@


clean:
	rm -rf $(PROG) *.o
