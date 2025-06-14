#ifndef _LINUX_SCHED_YAT_CASCHED_H
#define _LINUX_SCHED_YAT_CASCHED_H

#include <linux/list.h>
#include <linux/spinlock.h>

struct yatcasched_task {
    struct list_head node;
    int priority;
    u64 last_ran;
};

struct yatcasched_rq {
    struct list_head queue;
    struct task_struct *curr;
    unsigned int nr_running;
    raw_spinlock_t lock;
};

void init_yatcasched_rq(struct yatcasched_rq *yatcasched_rq);

#endif