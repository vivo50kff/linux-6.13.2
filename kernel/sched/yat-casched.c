#include "sched.h"

static void enqueue_task_yatcasched(struct rq *rq, struct task_struct *p, int flags) { }
static bool dequeue_task_yatcasched(struct rq *rq, struct task_struct *p, int flags) { return false; }
static void yield_task_yatcasched(struct rq *rq) { }
static struct task_struct *pick_next_task_yatcasched(struct rq *rq, struct task_struct *prev) { return NULL; }
static void put_prev_task_yatcasched(struct rq *rq, struct task_struct *p, struct task_struct *next) { }
static void task_tick_yatcasched(struct rq *rq, struct task_struct *p, int queued) { }
void init_yatcasched_rq(struct yatcasched_rq *yatcasched_rq) { }

const struct sched_class yatcasched_sched_class = {
    .enqueue_task   = enqueue_task_yatcasched,
    .dequeue_task   = dequeue_task_yatcasched,
    .yield_task     = yield_task_yatcasched,
    .pick_next_task = pick_next_task_yatcasched,
    .put_prev_task  = put_prev_task_yatcasched,
    .task_tick      = task_tick_yatcasched,
};