#
# Copyright (c) 2020, ETH Zurich.
# All rights reserved.
#
# This file is distributed under the terms in the attached license file.
# if you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
#

CC=gcc

INC=-I../../build/include
LIB=-L../../build/lib
CFLAGS=-g -Wall -Werror -Wextra -std=gnu11

CLEANQ_LIBS=-lcleanq -lrt
CLEANQ_LIBTS_STATIC=../../build/lib/libcleanq.a -lrt

all: exffqclient exffqserver

exffqserver: server.c ../../build/lib/libcleanq.a
	$(CC) $(CFLAGS) $(INC) -o $@ server.c $(CLEANQ_LIBTS_STATIC)

exffqclient:  client.c ../../build/lib/libcleanq.a
	$(CC) $(CFLAGS) $(INC) -o $@ client.c $(CLEANQ_LIBTS_STATIC)


build : all
	rsync -a exffqserver* ../../build/bin
	rsync -a exffqclient* ../../build/bin

clean:
	rm -rf exffqserver exffqclient
