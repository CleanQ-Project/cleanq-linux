#
# Copyright (c) 2020, ETH Zurich.
# All rights reserved.
#
# This file is distributed under the terms in the attached license file.
# if you do not find this file, copies can be found by writing to:
# ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
#

CLEANQ_TESTS=cleanqecho cleanqclient

all: $(CLEANQ_TESTS)

cleanqecho:
	make -C echoserver

cleanqclient:
	make -C client

build:
	make -C echoserver build
	make -C client build

clean:
	make -C echoserver clean
	make -C client clean
