/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef CLEANQ_DEBUGQ_H_
#define CLEANQ_DEBUGQ_H_ 1

#include <cleanq.h>

///< forward declaration of opaque type
struct cleanq_debugq;


/**
 * @brief creates a debug queue as a wrapper around another
 *
 * @param q         the created debug queue
 * @param other_q   the other queue to be wrapped
 *
 * @returns CLEANQ_ERR_OK on success, errval on failure
 */
errval_t cleanq_debugq_create(struct cleanq_debugq **q, struct cleanq *other_q);


/**
 * @brief dumps the information about a memory region
 *
 * @param q     the debug queue
 * @param rid   the memory region to be dumped
 */
void cleanq_debugq_dump_region(struct cleanq_debugq *q, regionid_t rid);


/**
 * @brief dumps the history of the debug queue
 *
 * @param q     the debug queue
 *
 */
void cleanq_debugq_dump_history(struct cleanq_debugq *q);


/**
 * @brief Adding region to debug queue. When using two endpoints
 *        Only the lowest layer is consistent with the regions
 *        To mache the debugging layer consistent, this method adds the region
 *        to the known regions so that the ckecks when dequeueing work
 *
 * @param q                     Return pointer to the descriptor queue
 * @param cap                   cap to the region
 * @param rid                  the regionid of the region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_debugq_add_region(struct cleanq_debugq *, struct capref cap, regionid_t rid);


/**
 * @brief Removing region from debug queue
 *
 * @param q                    Return pointer to the descriptor queue
 * @param rid                  the regionid of the region
 *
 * @returns error on failure or CLEANQ_ERR_OK on success
 */
errval_t cleanq_debugq_remove_region(struct cleanq_debugq *, regionid_t rid);

#endif /* CLEANQ_DEBUGQ_H_ */
