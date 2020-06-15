#
# Copyright (c) 2020, ETH Zurich.
# All rights reserved.
#
# This file is distributed under the terms in the attached license file.
# if you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
#

CC=gcc

# switch to enable benchmarking for the queues
ENABLE_BENCH=n


INC=-I../../build/include
LIB=-L../../build/lib
CFLAGS=-g -Wall -Werror -Wextra -std=gnu11


ifeq ($(ENABLE_BENCH),y)
	# adding benchmarking
	CFLAGS+=-DBENCH_CLEANQ
	INC+=-I./bench
endif



CLEANQ_LIBS=-lcleanq -lrt
CLEANQ_LIBTS_STATIC=../../build/lib/libcleanq.a -lrt

all: echoserver echoserverstatic echoclient echoclientstatic

echoserver: server.c
	$(CC) $(CFLAGS) $(INC) -o $@ server.c $(LIB) $(CLEANQ_LIBS)

echoserverstatic: server.c
	$(CC) $(CFLAGS) $(INC) -o $@ server.c $(CLEANQ_LIBTS_STATIC)

echoclient: client.c
	$(CC) $(CFLAGS) $(INC) -o $@ client.c $(LIB) $(CLEANQ_LIBS)

echoclientstatic:  client.c
	$(CC) $(CFLAGS) $(INC) -o $@ client.c $(CLEANQ_LIBTS_STATIC)


build : all
	rsync -a echoserver* ../../build/bin
	rsync -a echoclient* ../../build/bin

clean:
	rm -rf echo echostatic
