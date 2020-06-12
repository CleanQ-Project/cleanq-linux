/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */
#ifndef CLEANQ_IPCQ_H_
#define CLEANQ_IPCQ_H_ 1

#include <cleanq.h>

///< forwrd declaration
struct cleanq_ipcq;


/**
 * @brief initialized a ipc descriptor queue
 *
 * @param q         Return pointer to the descriptor queue
 * @param name      Name of the memory use for sending/receiving messages
 * @param clear     Clear the backing memory by zeroing
 * @param f         Function pointers to be called on message recv
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */

errval_t cleanq_ipcq_create(struct cleanq_ipcq **q, char *name, bool clear);

#endif /* CLEANQ_IPCQ_H_ */
