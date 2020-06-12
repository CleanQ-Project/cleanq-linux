#
# Copyright (c) 2020, ETH Zurich.
# All rights reserved.
#
# This file is distributed under the terms in the attached license file.
# if you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
#

all: cleanqbuild

cleanqbuild :
	mkdir -p build/bin
	mkdir -p build/include
	mkdir -p build/lib
	make -C cleanq build
	make -C tests build
	make -C examples build

clean:
	rm -rf build
	make -C cleanq clean
	make -C tests clean
	make -C examples clean
