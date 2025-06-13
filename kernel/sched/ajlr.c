
#include "sched.h"

static void enqueue_task_ajlr(struct rq *rq, struct task_struct *p, int flags) { }
static bool dequeue_task_ajlr(struct rq *rq, struct task_struct *p, int flags) { return false; }
static void yield_task_ajlr(struct rq *rq) { }
static struct task_struct *pick_next_task_ajlr(struct rq *rq, struct task_struct *prev) { return NULL; }
static void put_prev_task_ajlr(struct rq *rq, struct task_struct *p, struct task_struct *next) { }
static void task_tick_ajlr(struct rq *rq, struct task_struct *p, int queued) { }
void init_ajlr_rq(struct ajlr_rq *ajlr_rq) { }
const struct sched_class ajlr_sched_class = {
    .enqueue_task   = enqueue_task_ajlr,
    .dequeue_task   = dequeue_task_ajlr,
    .yield_task     = yield_task_ajlr,
    .pick_next_task = pick_next_task_ajlr,
    .put_prev_task  = put_prev_task_ajlr,
    .task_tick      = task_tick_ajlr,
};

