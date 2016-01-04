OBJ       = mempool.o sendfd.o command.o
PROG_OBJ  = ${OBJ} test.o
PROG_NAME = test
LIB_NAME  = libmempool

CC = gcc
AR = ar

COMMON_CFLAGS = -Wall -Werror -std=c11 -c -D_GNU_SOURCE
DEBUG_CFLAGS  = $(COMMON_CFLAGS) -g -O0
CFLAGS        = $(COMMON_CFLAGS) -O3 -DNDEBUG

LDFLAGS =
LIBS    = -lrt


$(PROG_NAME): $(PROG_OBJ)
	$(CC) $(LDFLAGS) -o $@ $(PROG_OBJ) $(LIBS)

lib: CFLAGS += -fPIC
lib: $(OBJ)
	$(CC) -shared $(LDFLAGS) $(LIBS) -o $(LIB_NAME).so $(OBJ)
	$(AR) -cvq $(LIB_NAME).a $(OBJ)

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(PROG_NAME)

%.c:
	$(CC) $(DCFLAGS) $*.c

mempool.o: mempool.h atomic.h
sendfd.o: sendfd.h

clean:
	rm -f $(PROG_OBJ) $(PROG_NAME) $(LIB_NAME).* *~ #*#

.PHONY: debug
