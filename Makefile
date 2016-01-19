OBJ       = mempool.o sendfd.o command.o

PROG_OBJ_SP_SC  = ${OBJ} test_sp_sc.o
PROG_NAME_SP_SC = test_sp_sc

PROG_OBJ_MP_MC  = ${OBJ} test_mp_mc.o
PROG_NAME_MP_MC = test_mp_mc

LIB_NAME  = libmempool

CC = gcc
AR = ar

COMMON_CFLAGS = -Wall -Werror -std=c11 -c -D_GNU_SOURCE
DEBUG_CFLAGS  = $(COMMON_CFLAGS) -g -O0
CFLAGS        = $(COMMON_CFLAGS) -O3 -DNDEBUG

LDFLAGS =
LIBS    = -lrt

all: $(PROG_NAME_SP_SC) $(PROG_NAME_MP_MC)

$(PROG_NAME_SP_SC): $(PROG_OBJ_SP_SC)
	$(CC) $(LDFLAGS) -o $@ $(PROG_OBJ_SP_SC) $(LIBS)

$(PROG_NAME_MP_MC): $(PROG_OBJ_MP_MC)
	$(CC) $(LDFLAGS) -o $@ $(PROG_OBJ_MP_MC) $(LIBS)

lib: CFLAGS += -fPIC
lib: $(OBJ)
	$(CC) -shared $(LDFLAGS) $(LIBS) -o $(LIB_NAME).so $(OBJ)
	$(AR) -cvq $(LIB_NAME).a $(OBJ)

debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(PROG_NAME_SP_SC) $(PROG_NAME_MP_MC)

%.c:
	$(CC) $(DCFLAGS) $*.c

mempool.o: mempool.h atomic.h mp_ring.h
sendfd.o:  sendfd.h

clean:
	rm -f $(PROG_OBJ_SP_SC) $(PROG_NAME_SP_SC)
	rm -f $(PROG_OBJ_MP_MC) $(PROG_NAME_MP_MC)
	rm -f $(LIB_NAME).* *~ #*#

.PHONY: debug
.PHONY: all
