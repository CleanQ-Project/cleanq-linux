#
# Copyright (c) 2020, ETH Zurich.
# All rights reserved.
#
# This file is distributed under the terms in the attached license file.
# if you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
#

# switch to enable benchmarking for the queues
ENABLE_BENCH=n

# switch to enable debug
ENABLE_DEBUG=n

# specify the compiler
CC=gcc

# the library to be build
LIBRARY=libcleanq

# setting the include path
INC=-I./include  -I./utils -I./

# compiler flags
CFLAGS=-Wall -Werror -Wextra -std=gnu11  -fPIC -O3

# Backends
C_FILES_BACKENDS=$(shell find backends/ -type f -name '*.c')
H_FILES_BACKENDS=$(shell find backends/ -type f -name '*.h')

# CleanQ
C_FILES_CLEANQ=$(wildcard *.c)
H_FILES_CLEANQ=$(wildcard *.h) $(wildcard utils/*.h)

# Public Headers
INCLUDES=$(wildcard include/*.h) $(wildcard include/backends/*h)

# all files
C_FILES=$(C_FILES_BACKENDS) $(C_FILES_CLEANQ)
H_FILES=$(H_FILES_BACKENDS) $(H_FILES_CLEANQ) $(INCLUDES)

ifeq ($(ENABLE_BENCH),y)
	# adding benchmarking
	C_FILES+=utils/bench.c
	CFLAGS+=-DBENCH_CLEANQ
	INC+=-I./bench
endif

ifeq ($(ENABLE_DEBUG),y)
	CFLAGS+=-g
else
	CFLAGS+=-DNDEBUG
endif


OBJ_FILES=$(C_FILES:%.c=%.o)

all: $(LIBRARY).a $(LIBRARY).so

%.o: %.c $(H_FILES) Makefile
	$(CC) $(CFLAGS) $(INC) -o $@ -c $<

$(LIBRARY).so: $(C_FILES) $(H_FILES) Makefile
	$(CC) -shared -o $@ $(CFLAGS) $(INC) $(C_FILES)

$(LIBRARY).a: $(OBJ_FILES) Makefile
	ar rcs $@ $(OBJ_FILES)

build: $(LIBRARY).a $(LIBRARY).so $(INCLUDES)
	rsync -a include/ ../build/include/cleanq
	rsync $(LIBRARY).a ../build/lib
	rsync $(LIBRARY).so ../build/lib

clean:
	rm -rf $(LIBRARY)
