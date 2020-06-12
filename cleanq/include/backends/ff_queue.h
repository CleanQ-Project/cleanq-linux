/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */
#ifndef CLEANQ_FF_QUEUE_H_
#define CLEANQ_FF_QUEUE_H_ 1

#include <cleanq.h>

///< forward declaration
struct cleanq_ffq;


/**
 * @brief initialized a the ffq backend
 *
 * @param q         Return pointer to the descriptor queue
 * @param name      Name of the memory use for sending messages
 * @param clear     Write 0 to memory
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_ffq_create(struct cleanq_ffq **q, const char *name, bool clear);

#endif /* CLEANQ_FF_QUEUE_H_ */
