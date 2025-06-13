// SPDX-License-Identifier: GPL-2.0
/*
 * AJLR scheduling class - MINIMAL VERSION FOR TESTING FRAMEWORK
 * All functions are empty or return simple values to test if framework works
 */

#include "sched.h"

/*
 * 初始化 AJLR 运行队列 - 最小化实现
 */
void init_ajlr_rq(struct ajlr_rq *ajlr_rq)
{
    printk("AJLR: Initializing AJLR runqueue (minimal version)\n");
    raw_spin_lock_init(&ajlr_rq->lock);
    INIT_LIST_HEAD(&ajlr_rq->queue);
    ajlr_rq->nr_running = 0;
    ajlr_rq->curr = NULL;
}

/*
 * 将任务加入 AJLR 运行队列 - 空实现
 */
static void enqueue_task_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    // 空实现，什么都不做
    return;
}

/*
 * 从 AJLR 运行队列中移除任务 - 空实现
 */
static bool dequeue_task_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    // 空实现，直接返回 false
    return false;
}

/*
 * 检查是否需要抢占当前运行的任务 - 空实现
 */
static void check_preempt_curr_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    // 空实现，什么都不做
    return;
}

/*
 * 选择下一个要执行的任务 - 总是返回 NULL
 */
static struct task_struct *pick_next_task_ajlr(struct rq *rq, struct task_struct *prev)
{
    // 空实现，总是返回 NULL（让其他调度类处理）
    return NULL;
}

/*
 * 前一个任务离开 CPU 时调用 - 空实现
 */
static void put_prev_task_ajlr(struct rq *rq, struct task_struct *p, struct task_struct *next)
{
    // 空实现，什么都不做
    return;
}

/*
 * 新任务被选为当前任务时调用 - 空实现
 */
static void set_next_task_ajlr(struct rq *rq, struct task_struct *p, bool first)
{
    // 空实现，什么都不做
    return;
}

/*
 * 任务主动让出 CPU 时调用 - 空实现
 */
static void yield_task_ajlr(struct rq *rq)
{
    // 空实现，什么都不做
    return;
}

/*
 * 时钟中断处理 - 空实现
 */
static void task_tick_ajlr(struct rq *rq, struct task_struct *p, int queued)
{
    // 空实现，什么都不做
    return;
}

/*
 * 任务被唤醒后调用 - 空实现
 */
static void task_woken_ajlr(struct rq *rq, struct task_struct *p)
{
    // 空实现，什么都不做
    return;
}

/*
 * 获取任务的时间片长度 - 返回固定值
 */
static unsigned int get_rr_interval_ajlr(struct rq *rq, struct task_struct *task)
{
    return 100; // 返回固定值
}

/*
 * 为 AJLR 任务选择最合适的 CPU - 返回当前 CPU
 */
static int select_task_rq_ajlr(struct task_struct *p, int cpu, int flags)
{
    return cpu; // 直接返回传入的 CPU
}

/*
 * 当任务优先级改变时调用 - 空实现
 */
static void prio_changed_ajlr(struct rq *rq, struct task_struct *p, int oldprio)
{
    // 空实现，什么都不做
    return;
}

/*
 * 当任务切换到 AJLR 调度类时调用 - 空实现
 */
static void switched_to_ajlr(struct rq *rq, struct task_struct *p)
{
    // 空实现，什么都不做
    return;
}

/*
 * AJLR 调度类的定义 - 最小化版本
 */
DEFINE_SCHED_CLASS(ajlr) = {
    
    .enqueue_task           = enqueue_task_ajlr,
    .dequeue_task           = dequeue_task_ajlr,
    .yield_task             = yield_task_ajlr,
    
    .wakeup_preempt         = check_preempt_curr_ajlr,
    
    .pick_next_task         = pick_next_task_ajlr,
    .put_prev_task          = put_prev_task_ajlr,
    
    .set_next_task          = set_next_task_ajlr,
    .task_tick              = task_tick_ajlr,
    .task_woken             = task_woken_ajlr,
    
    .get_rr_interval        = get_rr_interval_ajlr,
    
    .prio_changed           = prio_changed_ajlr,
    .switched_to            = switched_to_ajlr,
    
    .select_task_rq         = select_task_rq_ajlr,
};