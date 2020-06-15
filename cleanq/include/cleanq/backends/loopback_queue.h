/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef CLEANQ_BACKEND_LOOPBACK_QUEUE_H_
#define CLEANQ_BACKEND_LOOPBACK_QUEUE_H_

#include <cleanq/cleanq.h>

///< forward declaration of the queue type
struct cleanq_loopbackq;

/**
 * @brief creates a new loopback queue
 *
 * @param q     returned pointer to the newly created queue
 *
 * @returns CLEANQ_ERR_OK - on success
 *          CLEANQ_ERR_MALLOC_FAIL - if the queue could not be initialized
 */
errval_t loopback_queue_create(struct cleanq_loopbackq **q);

#endif  // CLEANQ_BACKEND_LOOPBACK_QUEUE_H_
