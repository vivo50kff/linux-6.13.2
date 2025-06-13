/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_AJLR_H
#define _LINUX_SCHED_AJLR_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct ajlr_task {
    struct list_head node;
    int priority;
    u64 last_ran;
};

struct ajlr_rq {
    struct list_head queue;
    struct task_struct *curr;
    unsigned int nr_running;
    raw_spinlock_t lock;
};

void init_ajlr_rq(struct ajlr_rq *ajlr_rq);

#endif