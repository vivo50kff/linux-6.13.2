# AJLR 缓存感知调度器：Linux 内核实现指南

## 简介

本文档提供了将 AJLR (Adaptive Job-Level Reclaiming) 缓存感知调度算法实现到 Linux 内核的详细指南。AJLR 是一种旨在通过充分利用缓存亲和性来优化任务分配和调度的算法，它基于任务在不同 CPU 核心上的预期执行时间差异（"加速值"）来做出调度决策。

在Linux 6.11+版本中，RT-preempt（实时抢占）补丁已被正式合并到主线内核中，这为实现具有强实时性能的AJLR调度器提供了坚实基础。我们的实现将充分利用内置的实时功能，包括完全抢占和优先级继承机制等。

## 第一阶段：准备工作

### 获取 Linux 内核源码

```bash
git clone https://github.com/torvalds/linux.git
cd linux
git checkout v6.13.2  # 此版本已内置RT-preempt补丁
```

### 创建开发分支

```bash
git checkout -b ajlr-scheduler
```

### 安装内核开发依赖项

```bash
# 在 Ubuntu/Debian 上
sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev
```

## 第二阶段：文件创建与修改

### 创建 AJLR 调度器的配置选项

在Linux 6.13.2中，调度器相关的配置选项位于`kernel/Kconfig.preempt`文件中。修改该文件：


在文件中找到其他调度器类配置（如SCHED_CORE或SCHED_CLASS_EXT）附近（大概在135行），添加以下内容：

```
config SCHED_AJLR
    bool "AJLR Cache-Aware Scheduler Class"
    depends on SMP
    default n
    help
      This option enables the AJLR cache-aware scheduler which
      optimizes task placement on CPUs based on cache utilization
      and tasks' cache footprints to minimize cache-related delays.

      If unsure, say N.
```

### 创建 AJLR 调度器的头文件

创建文件 `include/linux/sched/ajlr.h`：

```c
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_AJLR_H
#define _LINUX_SCHED_AJLR_H

#include <linux/sched.h>
#include <linux/spinlock.h>

#ifdef CONFIG_SCHED_AJLR

/* AJLR 任务的特定数据，嵌入在 task_struct 中 */
struct ajlr_task {
    long wcet;                  /* 最坏情况执行时间 */
    int last_cpu;               /* 上次执行的 CPU */
    struct list_head run_node;  /* 运行队列的链表节点 */
    bool scheduled;             /* 是否已被调度 */
    unsigned int time_slice;    /* 时间片 */
};

/* 每个 CPU 的 AJLR 运行队列 */
struct ajlr_rq {
    raw_spinlock_t lock;        /* 保护运行队列的锁 */
    struct list_head queue;     /* AJLR 任务的运行队列 */
    unsigned int nr_running;    /* 运行队列中的任务数 */
    struct task_struct *curr;   /* 当前运行的 AJLR 任务 */
};

/* AJLR 调度器配置参数 */
#define AJLR_CACHE_BONUS_PCT 10 /* 缓存命中带来的执行时间减少百分比 */

/* 函数声明 */
extern void init_ajlr_rq(struct ajlr_rq *ajlr_rq);

#endif /* CONFIG_SCHED_AJLR */
#endif /* _LINUX_SCHED_AJLR_H */
```

### 修改 task_struct 结构体

修改 `include/linux/sched.h`，首先在文件顶部添加 AJLR 头文件的包含：

```c
/* 现有头文件包含 */

/* 确保包含 AJLR 头文件 */
#include <linux/sched/ajlr.h>
```

然后在 `task_struct` 中添加 AJLR 相关字段(查找为第785行开始，这里在1608行添加)：

```c
struct task_struct {
    /* ... 现有字段 ... */

#ifdef CONFIG_SCHED_AJLR
    struct ajlr_task     ajlr;  /* AJLR 调度器特定数据 */
#endif
    
    /* ... 现有字段 ... */
};
```

同时添加新的调度策略常量：

```c
/* 调度策略 */
#define SCHED_NORMAL        0
#define SCHED_FIFO          1
#define SCHED_RR            2
#define SCHED_BATCH         3
/* ... 其他现有策略 ... */
#define SCHED_AJLR          9  /* 新的 AJLR 策略 */
```

### 修改运行队列结构体

修改 `kernel/sched/sched.h`，在 `rq` 结构中添加 AJLR 运行队列：

```c
struct rq {
    /* ... 现有字段 ... */

#ifdef CONFIG_SCHED
    struct ajlr_rq        ajlr;  /* AJLR 运行队列 */
#endif
    
    /* ... 现有字段 ... */
};
```

同时添加 AJLR 调度类的前向声明：

```c
#ifdef CONFIG_SCHED_AJLR
extern const struct sched_class ajlr_sched_class;
#endif
```

### 实现 AJLR 调度器核心文件（简化 RR 版本）

创建文件 `kernel/sched/ajlr.c`，先实现一个简化的时间片轮转调度器来验证框架：

```c
// SPDX-License-Identifier: GPL-2.0
/*
 * AJLR scheduling class - simplified as Round Robin for initial testing
 */

#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/deadline.h>
#include <linux/sched/debug.h>
#include <linux/sched/sysctl.h>
#include <linux/sched/topology.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/ajlr.h>
#include <linux/spinlock.h>

#include "sched.h"

/* 默认时间片长度（以纳秒为单位） */
#define AJLR_TIMESLICE (100 * NSEC_PER_MSEC)

/*
 * 初始化 AJLR 运行队列
 */
void init_ajlr_rq(struct ajlr_rq *ajlr_rq)
{
    raw_spin_lock_init(&ajlr_rq->lock);
    INIT_LIST_HEAD(&ajlr_rq->queue);
    ajlr_rq->nr_running = 0;
    ajlr_rq->curr = NULL;
}

/*
 * 将任务加入 AJLR 运行队列
 */
static void enqueue_task_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    struct ajlr_rq *ajlr_rq = &rq->ajlr;
    struct ajlr_task *ajlr_se = &p->ajlr;
    
    if (list_empty(&ajlr_se->run_node)) {
        list_add_tail(&ajlr_se->run_node, &ajlr_rq->queue);
        ajlr_se->scheduled = true;
        ajlr_rq->nr_running++;
    }
}

/*
 * 从 AJLR 运行队列中移除任务，返回 bool 类型
 */
static bool dequeue_task_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    struct ajlr_rq *ajlr_rq = &rq->ajlr;
    struct ajlr_task *ajlr_se = &p->ajlr;
    bool was_queued = false;
    
    if (!list_empty(&ajlr_se->run_node) && ajlr_se->scheduled) {
        list_del_init(&ajlr_se->run_node);
        ajlr_se->scheduled = false;
        ajlr_rq->nr_running--;
        was_queued = true;
    }
    
    return was_queued;
}

/*
 * 检查是否需要抢占当前运行的任务
 */
static void check_preempt_curr_ajlr(struct rq *rq, struct task_struct *p, int flags)
{
    // 简单的时间片轮转不进行抢占
    return;
}

/*
 * 选择下一个要执行的任务 - 修正函数签名
 */
static struct task_struct *pick_next_task_ajlr(struct rq *rq, struct task_struct *prev)
{
    struct ajlr_rq *ajlr_rq = &rq->ajlr;
    struct task_struct *next = NULL;
    struct task_struct *p;
    struct ajlr_task *ajlr_se;
    
    // 如果没有可运行的任务，返回 NULL
    if (!ajlr_rq->nr_running)
        return NULL;
    
    // 如果上一个任务的时间片已经用完，移到链表末尾
    if (prev && prev->sched_class == &ajlr_sched_class) {
        struct ajlr_task *prev_se = &prev->ajlr;
        if (prev_se->time_slice == 0) {
            list_del(&prev_se->run_node);
            list_add_tail(&prev_se->run_node, &ajlr_rq->queue);
            prev_se->time_slice = AJLR_TIMESLICE;
        }
    }
    
    // 从队列头部取出下一个任务
    if (!list_empty(&ajlr_rq->queue)) {
        ajlr_se = list_first_entry(&ajlr_rq->queue, struct ajlr_task, run_node);
        p = container_of(ajlr_se, struct task_struct, ajlr);
        next = p;
        ajlr_rq->curr = next;
    }
    
    return next;
}

/*
 * 前一个任务离开 CPU 时调用 - 修正函数签名
 */
static void put_prev_task_ajlr(struct rq *rq, struct task_struct *p, struct task_struct *next)
{
    // 在 RR 中，我们在 pick_next_task 中已经处理了时间片耗尽的情况
    return;
}

/*
 * 新任务被选为当前任务时调用
 */
static void set_next_task_ajlr(struct rq *rq, struct task_struct *p, bool first)
{
    struct ajlr_task *ajlr_se = &p->ajlr;
    
    p->ajlr.last_cpu = smp_processor_id();
    
    // 设置初始时间片
    if (ajlr_se->time_slice == 0)
        ajlr_se->time_slice = AJLR_TIMESLICE;
}

/*
 * 任务主动让出 CPU 时调用
 */
static void yield_task_ajlr(struct rq *rq)
{
    struct ajlr_rq *ajlr_rq = &rq->ajlr;
    struct task_struct *curr = rq->curr;
    struct ajlr_task *ajlr_se = &curr->ajlr;

    // 将当前任务移到队列末尾
    if (!list_empty(&ajlr_se->run_node)) {
        list_del(&ajlr_se->run_node);
        list_add_tail(&ajlr_se->run_node, &ajlr_rq->queue);
    }
    
    set_tsk_need_resched(curr);
}

/*
 * 时钟中断处理，更新时间片
 */
static void task_tick_ajlr(struct rq *rq, struct task_struct *p, int queued)
{
    struct ajlr_task *ajlr_se = &p->ajlr;
    
    // 减少时间片
    if (ajlr_se->time_slice > 0)
        ajlr_se->time_slice--;
        
    // 如果时间片用完，设置重调度标志
    if (ajlr_se->time_slice == 0) {
        resched_curr(rq);
    }
}

/*
 * 任务被唤醒后调用
 */
static void task_woken_ajlr(struct rq *rq, struct task_struct *p)
{
    // 简单实现，不需要特殊处理
}

/*
 * 获取任务的时间片长度
 */
static unsigned int get_rr_interval_ajlr(struct rq *rq, struct task_struct *task)
{
    return AJLR_TIMESLICE;
}

/*
 * 为 AJLR 任务选择最合适的 CPU
 */
static int select_task_rq_ajlr(struct task_struct *p, int cpu, int flags)
{
    // 简单实现，返回当前 CPU 或上一个运行的 CPU
    if (p->ajlr.last_cpu != -1)
        return p->ajlr.last_cpu;
    return cpu;
}

/*
 * 当任务优先级改变时调用
 */
static void prio_changed_ajlr(struct rq *rq, struct task_struct *p, int oldprio)
{
    // 简单实现，不需要特殊处理
}

/*
 * 当任务切换到 AJLR 调度类时调用
 */
static void switched_to_ajlr(struct rq *rq, struct task_struct *p)
{
    if (task_on_rq_queued(p) && rq->curr != p)
        resched_curr(rq);
}

/*
 * AJLR 调度类的定义 - 使用与 Linux 6.13 兼容的接口
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
```

### 修改调度器层

在Linux 6.13.2内核的`kernel/sched/core.c`文件中需要进行两处主要修改：

#### 第一处修改：在 `sched_init()` 函数开头添加调度类检查

在`sched_init()`函数的开头部分，通常有一系列的`BUG_ON()`检查，用于确保调度类的层次结构正确。需要修改这部分代码来添加对AJLR调度类位置的检查（大约在第 8468 行附近）：

```c
/* Make sure the linker didn't screw up */
#ifdef CONFIG_SMP
    BUG_ON(!sched_class_above(&stop_sched_class, &dl_sched_class));
#endif
BUG_ON(!sched_class_above(&dl_sched_class, &rt_sched_class));
BUG_ON(!sched_class_above(&rt_sched_class, &fair_sched_class));
BUG_ON(!sched_class_above(&fair_sched_class, &idle_sched_class));
```

修改为：

```c
/* Make sure the linker didn't screw up */
#ifdef CONFIG_SMP
    BUG_ON(!sched_class_above(&stop_sched_class, &dl_sched_class));
#endif
BUG_ON(!sched_class_above(&dl_sched_class, &rt_sched_class));
#ifdef CONFIG_SCHED_AJLR
    BUG_ON(!sched_class_above(&rt_sched_class, &ajlr_sched_class));
    BUG_ON(!sched_class_above(&ajlr_sched_class, &fair_sched_class));
#else
    BUG_ON(!sched_class_above(&rt_sched_class, &fair_sched_class));
#endif
BUG_ON(!sched_class_above(&fair_sched_class, &idle_sched_class));
```

#### 第二处修改：在 `sched_init()` 函数中添加 AJLR 运行队列初始化

查找 `sched_init()` 函数中类似如下代码段：（大约在8536行）

```c
for_each_possible_cpu(i) {
    struct rq *rq = cpu_rq(i);
    
    raw_spin_lock_init(&rq->__lock);
    rq->nr_running = 0;
    rq->calc_load_active = 0;
    rq->calc_load_update = jiffies + LOAD_FREQ;
    init_cfs_rq(&rq->cfs);
    init_rt_rq(&rq->rt);
    init_dl_rq(&rq->dl);
    
    /* 其他初始化代码 */
}
```

在 `init_dl_rq(&rq->dl);` 这一行之后，添加 AJLR 运行队列的初始化代码：

```c
#ifdef CONFIG_SCHED_AJLR
init_ajlr_rq(&rq->ajlr);
#endif
```

注意：调度类的定义位置可能不在`core.c`文件中，而是在其他相关头文件中。在修改前，请先使用以下命令找到调度类定义所在位置：

```bash
grep -r "DEFINE_SCHED_CLASS" kernel/
```

然后修改相应文件，确保AJLR调度类正确位于RT和FAIR调度类之间。

### 更新 Makefile

修改 `kernel/sched/Makefile`，添加：

```makefile
obj-$(CONFIG_SCHED_AJLR) += ajlr.o
```

### 编译和运行测试程序

```bash
gcc -o test_ajlr test_ajlr.c
./test_ajlr
```

### 常见编译错误及解决方案

在实现 AJLR 调度器时，您可能会遇到一些常见的编译错误。以下是一些可能的错误和解决方案：

1. **错误: `unknown type name 'raw_spinlock_t'`**
   ```
   ./include/linux/sched/ajlr.h:19:5: error: unknown type name 'raw_spinlock_t'
   ```
   **解决方案:** 在 `ajlr.h` 中包含 `<linux/spinlock.h>` 头文件。（我们已经在上面的头文件中包含了）

2. **错误: `struct ajlr_task` 缺少 `time_slice` 成员**
   ```
   kernel/sched/ajlr.c:80:20: error: 'struct ajlr_task' has no member named 'time_slice'
   ```
   **解决方案:** 在 `struct ajlr_task` 中添加 `unsigned int time_slice;` 成员。（我们已经在上面的结构体中添加了）

3. **错误: 调度类接口不匹配**
   ```
   kernel/sched/ajlr.c:155:6: error: 'const struct sched_class' has no member named 'set_curr_task'
   ```
   **解决方案:** 在 Linux 6.13.2 中，一些函数名称已经改变，例如 `set_curr_task` 已经改为 `set_next_task`。我们已经在上面的实现中使用了正确的接口。

4. **错误: 重复定义 `struct ajlr_rq`**
   ```
   kernel/sched/ajlr.c:25:8: error: redefinition of 'struct ajlr_rq'
   ```
   **解决方案:** 确保 `struct ajlr_rq` 只在 `ajlr.h` 中定义一次，在 `ajlr.c` 中使用该定义而不是重新定义。

5. **错误: `ajlr_sched_class` 未声明**
   ```
   kernel/sched/ajlr.c:78:39: error: 'ajlr_sched_class' undeclared
   ```
   **解决方案:** 确保在使用 `ajlr_sched_class` 之前已经声明它，通常在 `sched.h` 中添加前向声明。

### 使调度器与内核接口兼容的关键点

1. **保持调度类接口一致**: 确保 `ajlr_sched_class` 的结构与内核中其他调度类（如 `fair_sched_class`）的接口一致。查看 `kernel/sched/sched.h` 中的调度类定义。

2. **使用正确的函数签名**: 调度类中的回调函数必须与内核期望的签名完全匹配，包括参数类型和返回类型。

3. **使用 `__section` 属性**: 可以使用 `__section("__ajlr_sched_class")` 属性来确保调度类放置在正确的内存段中。

4. **处理依赖关系**: 确保 `next` 指针正确设置，例如 `ajlr_sched_class.next = &fair_sched_class`，这决定了调度类的优先级顺序。

5. **遵循内核代码风格**: 使用与内核其他部分一致的命名和代码风格。可以使用 `scripts/checkpatch.pl` 检查代码风格。

记住，内核接口可能会在不同版本之间改变，所以总是参考您正在使用的特定内核版本的源代码。

## 第三阶段：简化实现与测试

为了验证调度框架是否正确集成，我们首先可以将 `ajlr.c` 实现为一个简单的时间片轮转(RR)调度器。这个简化版本将帮助我们确认基础框架能够正常工作，然后再逐步添加更复杂的缓存感知功能。

### 编译内核与模块

配置内核并启用 AJLR 调度器：

```bash
cd /home/lwd/桌面/linux-6.13.2
make menuconfig
# 在 "General setup" 菜单中找到并启用 "AJLR Cache-Aware Scheduler Class"
# 注意：在菜单中还可以看到 "CPU/Task time and stats accounting" 选项，这是用于收集CPU和任务执行时间统计数据的功能
```

有多种编译选项，从最具体到最完整：

```bash
# 选项1：只编译 ajlr.c 文件（最快）
make kernel/sched/yat-casched.o -j$(nproc)

# 选项2：编译整个调度器模块
make M=kernel/sched/ -j$(nproc)

# 选项3：编译完整内核（需要更长时间）
make -j$(nproc)

# 安装编译好的内核
sudo make modules_install
sudo make install
```

### 使用 QEMU 测试

为了避免每次测试都重启物理机器，可以使用 QEMU 在虚拟环境中测试：

```bash
# 创建一个简单的 initramfs
mkdir -p initramfs/bin
cp /bin/busybox initramfs/bin/
cd initramfs 
ln -s bin/busybox init
cd bin
for cmd in $(./busybox --list); do ln -s busybox $cmd; done
cd ../..
find initramfs | cpio -o -H newc | gzip > initramfs.cpio.gz

# 使用 QEMU 启动内核
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0 nokaslr" \
  -serial stdio \
  -enable-kvm \
  -m 1G \
  -smp 4
```

### 关于 QEMU 的硬件加速问题

在使用 QEMU 测试时，如果遇到无法启用 KVM 硬件加速的问题，可以参考以下两种方法：

#### 方法 1：在 VMware 中启用嵌套虚拟化

如果您希望使用硬件加速（推荐，性能更好）：

1. 完全关闭您的 VMware 虚拟机。
2. 在 VMware 中编辑虚拟机设置。
3. 找到 CPU/处理器选项。
4. 启用"虚拟化 Intel VT-x/EPT 或 AMD-V/RVI"选项。
5. 保存设置并重启虚拟机。
6. 重新尝试加载 KVM 模块：

   ```bash
   sudo modprobe kvm_intel  # 如果是 Intel CPU
   sudo modprobe kvm_amd    # 如果是 AMD CPU
   ```

#### 方法 2：移除 QEMU 命令中的 -enable-kvm 参数

如果无法启用嵌套虚拟化，您可以在不使用硬件加速的情况下运行 QEMU。由于不同版本的 QEMU 可能有兼容性问题，请尝试以下几种启动方式：

```bash
# 检查 QEMU 版本
qemu-system-x86_64 --version

# 方法 2A：适用于较新版本的 QEMU (7.0+)
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0,115200 earlycon=uart8250,io,0x3f8 nokaslr" \
  -serial stdio \
  -display none \
  -m 1G \
  -smp 4

# 方法 2B：适用于较老版本的 QEMU (6.x及以下)
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append "console=ttyS0 console=tty0 nokaslr debug loglevel=8" \
  -serial stdio \
  -nographic \
  -m 1G \
  -smp 4

# 方法 2C：最兼容的启动方式（适用于所有版本）
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd initramfs.cpio.gz \
  -append "root=/dev/ram0 rdinit=/init console=ttyS0,115200n8 nokaslr" \
  -serial stdio \
  -m 1G \
  -smp 4
```

请注意，移除 `-enable-kvm` 参数后，QEMU 会使用纯软件模拟，性能会显著降低，但仍然可以用于功能性测试。

### 创建测试程序

创建一个简单的测试程序 `test_ajlr.c`，验证 AJLR 调度器能否正常工作：

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/sched.h>

// 定义 SCHED_AJLR 策略号（与内核中定义的相同）
#define SCHED_AJLR 9

int main() {
    struct sched_param param;
    param.sched_priority = 50;
    
    printf("Testing AJLR scheduler...\n");
    printf("Current scheduling policy: %d\n", sched_getscheduler(0));
    
    // 尝试切换到 AJLR 调度策略
    if (sched_setscheduler(0, SCHED_AJLR, &param) == -1) {
        perror("Failed to set AJLR scheduling policy");
        return 1;
    }
    
    printf("Switched to AJLR scheduling policy: %d\n", sched_getscheduler(0));
    
    // 运行一些CPU密集型代码
    printf("Running CPU-intensive loop...\n");
    int i, j, sum = 0;
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 10000000; j++) {
            sum += j % 100;
        }
        printf("Iteration %d complete, sum: %d\n", i, sum);
        // 让出CPU，允许其他任务运行
        sched_yield();
    }
    
    return 0;
}
```

编译并运行测试程序：

```bash
gcc -o test_ajlr test_ajlr.c
./test_ajlr
```

### 使用 ftrace 调试

ftrace 是 Linux 内核内置的跟踪工具，可以帮助观察调度器行为：

```bash
# 在 QEMU 环境中运行
mount -t debugfs debugfs /sys/kernel/debug
cd /sys/kernel/debug/tracing
echo 1 > tracing_on
echo function_graph > current_tracer
echo sched_switch > set_event
echo 1 > events/sched/sched_switch/enable

# 运行测试程序
/path/to/test_ajlr

# 查看跟踪结果
cat trace
```

### 调试 AJLR 调度器

如果遇到问题，可以使用以下方法进行调试：

```bash
# 检查内核日志
dmesg | grep AJLR

# 查看 trace_printk 输出
cat /sys/kernel/debug/tracing/trace_pipe

# 检查进程的调度策略
cat /proc/self/status | grep policy
```

### 从简单调度器到完整 AJLR

一旦验证了简化版调度器能正常工作，就可以逐步增强功能：

1. 添加基本的缓存亲和性逻辑
2. 实现基于 CPU 历史的任务分配
3. 添加 LCIF 机制
4. 实现完整的多级缓存模型

每次修改后，只需重新编译调度器模块并测试即可。

## 故障排除：解决内核启动崩溃问题

如果您一直遇到 "Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000009" 错误，很可能是 AJLR 调度器代码导致的问题。以下是系统性的故障排除方法：

### QEMU 启动问题的快速解决方案

既然您的 AJLR 调度器已经成功编译，问题很可能在 QEMU 的启动配置或 initramfs 设置上。让我们专注于解决 QEMU 启动问题：

#### 方法 1：不使用 initramfs 启动

```bash
cd /home/lwd/桌面/linux-6.13.2

# 直接启动内核，不使用 initramfs
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -append "console=ttyS0 init=/bin/bash" \
  -serial stdio \
  -m 1G \
  -smp 4
```

#### 方法 2：使用兼容的 initramfs（推荐）

```bash
# 创建兼容性更好的 initramfs
mkdir -p compat_initramfs/{bin,sbin,dev,proc,sys,etc}

# 复制静态链接的工具
cp /bin/busybox-static compat_initramfs/bin/busybox 2>/dev/null || \
cp /usr/bin/busybox compat_initramfs/bin/busybox

chmod +x compat_initramfs/bin/busybox

# 创建设备节点
mknod compat_initramfs/dev/console c 5 1
mknod compat_initramfs/dev/null c 1 3

# 创建兼容性更好的 init 脚本
cat > compat_initramfs/init << 'EOF'
#!/bin/sh
echo "Starting minimal init..."

# 挂载必要的文件系统
mount -t proc proc /proc 2>/dev/null
mount -t sysfs sysfs /sys 2>/dev/null
mount -t devtmpfs devtmpfs /dev 2>/dev/null

# 创建基本的符号链接
cd /bin
for cmd in sh ash ls cat echo mount; do
    ln -sf busybox $cmd 2>/dev/null
done

echo "Basic init complete. Starting shell..."
exec /bin/sh
EOF

chmod +x compat_initramfs/init

# 打包（使用兼容性更好的方式）
cd compat_initramfs
find . | cpio -o -H newc | gzip -9 > ../compat_initramfs.cpio.gz
cd ..

# 测试启动（多种兼容方式）
# 方式A：标准方式
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd compat_initramfs.cpio.gz \
  -append "console=ttyS0,115200 rdinit=/init" \
  -serial stdio \
  -display none \
  -m 1G \
  -smp 4

# 方式B：如果方式A不行，尝试这个
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd compat_initramfs.cpio.gz \
  -append "console=ttyS0 init=/init rw" \
  -serial stdio \
  -nographic \
  -m 1G \
  -smp 4
```

#### 方法 3：检查内核配置

```bash
# 检查内核镜像是否存在且大小合理
ls -lh arch/x86/boot/bzImage

# 检查内核是否包含必要的配置
grep -E "CONFIG_SERIAL_8250|CONFIG_VT|CONFIG_TTY" .config
```

## 总结

通过遵循本指南，可以将 AJLR 缓存感知调度算法实现为 Linux 内核中的一个新调度类。本实现通过识别和利用缓存亲和性，可以提高实时任务的执行效率，减少缓存相关的延迟，从而提高系统整体性能。使用渐进式开发方法，从简单的时间片轮转调度器开始，逐步添加缓存感知特性，能够确保实现的正确性和稳定性。

#### 关于"CPU/Task time and stats accounting"选项

"CPU/Task time and stats accounting" 是内核中的一个重要配置选项，它与调度器密切相关：

1. **功能说明**：该选项启用对 CPU 和任务时间的统计功能，包括：
   - 任务执行时间统计（如用户态/内核态时间）
   - CPU 使用率计算
   - 进程记账（process accounting）

2. **对调度器的影响**：
   - 为调度器提供重要的性能指标，如任务的实际执行时间
   - 使调度器能够基于历史执行数据做出更智能的决策
   - 对于缓存感知调度器（如 AJLR），这些统计数据尤为重要，可用于验证缓存亲和性带来的性能提升

3. **开启建议**：
   - 在开发和测试 AJLR 调度器时，建议保持该选项开启
   - 这将帮助收集与验证缓存命中/缺失对任务执行时间的影响
   - 可以与性能计数器一起使用，获取更详细的缓存利用数据

该选项虽然会增加一些系统开销，但对于调度器研发阶段的性能分析和调试非常有价值。

### 常见编译错误及解决方案

在实现 AJLR 调度器时，您可能会遇到一些常见的编译错误。以下是一些可能的错误和解决方案：

1. **错误: `unknown type name 'raw_spinlock_t'`**
   ```
   ./include/linux/sched/ajlr.h:19:5: error: unknown type name 'raw_spinlock_t'
   ```
   **解决方案:** 在 `ajlr.h` 中包含 `<linux/spinlock.h>` 头文件。

2. **错误: `struct ajlr_task` 缺少 `time_slice` 成员**
   ```
   kernel/sched/ajlr.c:80:20: error: 'struct ajlr_task' has no member named 'time_slice'
   ```
   **解决方案:** 在 `struct ajlr_task` 中添加 `unsigned int time_slice;` 成员。

3. **错误: 调度类接口不匹配**
   ```
   kernel/sched/ajlr.c:155:6: error: 'const struct sched_class' has no member named 'set_curr_task'
   ```
   **解决方案:** 参考最新版本的 Linux 内核源码中的调度类定义，更新调度类成员函数的名称和签名。在 Linux 6.13.2 中，一些函数名称可能已经改变，例如 `set_curr_task` 可能已经改为 `set_next_task`。

4. **错误: 重复定义 `struct ajlr_rq`**
   ```
   kernel/sched/ajlr.c:25:8: error: redefinition of 'struct ajlr_rq'
   ```
   **解决方案:** 确保 `struct ajlr_rq` 只在 `ajlr.h` 中定义一次，在 `ajlr.c` 中使用该定义而不是重新定义。

5. **错误: `ajlr_sched_class` 未声明**
   ```
   kernel/sched/ajlr.c:78:39: error: 'ajlr_sched_class' undeclared
   ```
   **解决方案:** 确保在使用 `ajlr_sched_class` 之前已经声明它，通常在 `sched.h` 中添加前向声明。

### 使调度器与内核接口兼容的关键点

1. **保持调度类接口一致**: 确保 `ajlr_sched_class` 的结构与内核中其他调度类（如 `fair_sched_class`）的接口一致。查看 `kernel/sched/sched.h` 中的调度类定义。

2. **使用正确的函数签名**: 调度类中的回调函数必须与内核期望的签名完全匹配，包括参数类型和返回类型。

3. **使用 `__section` 属性**: 可以使用 `__section("__ajlr_sched_class")` 属性来确保调度类放置在正确的内存段中。

4. **处理依赖关系**: 确保 `next` 指针正确设置，例如 `ajlr_sched_class.next = &fair_sched_class`，这决定了调度类的优先级顺序。

5. **遵循内核代码风格**: 使用与内核其他部分一致的命名和代码风格。可以使用 `scripts/checkpatch.pl` 检查代码风格。

记住，内核接口可能会在不同版本之间改变，所以总是参考您正在使用的特定内核版本的源代码。

## 编译问题的解决方案

### 解决 unplaced orphan section 错误

在编译完整内核时，可能会遇到以下错误：
```
ld: error: unplaced orphan section `__ajlr_sched_class' from `vmlinux.o'
```

这是因为链接器不知道如何放置 AJLR 调度类段。需要修改 `include/asm-generic/vmlinux.lds.h` 文件，在调度类定义中添加 AJLR 调度类：

```c
/*
 * The order of the sched class addresses are important, as they are
 * used to determine the order of the priority of each sched class in
 * relation to each other.
 */
#define SCHED_DATA				\
	STRUCT_ALIGN();				\
	__sched_class_highest = .;		\
	*(__stop_sched_class)			\
	*(__dl_sched_class)			\
	*(__rt_sched_class)			\
	*(__ajlr_sched_class)			\
	*(__fair_sched_class)			\
	*(__ext_sched_class)			\
	*(__idle_sched_class)			\
	__sched_class_lowest = .;
```

这样链接器就能正确放置 AJLR 调度类段，解决编译错误。调度类的顺序很重要，它决定了各调度类的优先级关系。在这里，我们将 AJLR 放在 RT 和 FAIR 调度类之间，表示其优先级高于 FAIR 但低于 RT。

## 关于编译性能问题

如果 `make -j$(nproc)` 编译过程耗时很长，可能由以下原因导致：

1. **虚拟机资源分配不足**：
   - 为虚拟机分配的 CPU 核心数太少。可以在 VMware 设置中增加分配的处理器数量
   - 内存不足。编译内核需要大量内存，建议至少分配 4GB 或更多
   - 虚拟机磁盘速度限制。考虑使用 SSD 而非 HDD 作为虚拟机存储

2. **优化编译选项**：
   - 使用 `-j` 参数时确保不超过实际可用的 CPU 核心数
   - 考虑使用 `ccache` 加速重复编译：`sudo apt-get install ccache`
   - 可以尝试增量编译，只编译修改过的部分

3. **减少编译内容**：
   - 如只需测试 AJLR 调度器，可以使用 `make kernel/sched/ajlr.o` 而非编译整个内核
   - 使用 `make localmodconfig` 创建仅包含当前系统所需驱动的配置

通过增加虚拟机的 CPU 核心数（建议至少 4 核），以及增加内存分配（建议至少 4GB），可以显著提升编译速度。