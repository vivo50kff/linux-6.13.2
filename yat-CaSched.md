# Yat-CASched（原AJLR）内核调度器开发记录

## 1. 项目简介

Yat-CASched（Yet Another Task Cache-Aware Scheduler）是基于 Linux 6.13.2 内核的自定义调度类。本文档记录了从空白调度类（白板）到添加调度策略号前的所有主要开发步骤。

## 2. 启动与测试流程

### 2.1 制作 initramfs

```bash
# 1.1 创建目录结构
mkdir -p compat_initramfs/bin

# 1.2 拷贝 busybox
cp /bin/busybox compat_initramfs/bin/

# 1.3 创建 init 脚本
cat > compat_initramfs/init <<'EOF'
#!/bin/sh
echo "Hello from initramfs!"
exec sh
EOF
chmod +x compat_initramfs/init

# 1.4 创建 busybox 的所有命令软链接（最重要的是 sh）
cd compat_initramfs/bin
for cmd in $(./busybox --list); do ln -sf busybox $cmd; done
cd ../..

# 1.5 确保 /bin/sh 存在
ln -sf busybox compat_initramfs/bin/sh

# 1.6 打包 initramfs
cd compat_initramfs
find . | cpio -o -H newc | gzip -9 > ../compat_initramfs.cpio.gz
cd ..
```

### 2.2 QEMU 启动命令

```bash
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd compat_initramfs.cpio.gz \
  -append "console=ttyS0,115200 rdinit=/init" \
  -serial stdio \
  -m 1G \
  -smp 4
```

- 如果只是重新编译内核（未更改 initramfs 内容），**不需要重新打包 initramfs**，直接用 QEMU 启动即可。
- 只有在更改了 initramfs 里的文件（如 busybox、init 脚本、测试程序等）时，才需要重新打包。

---

## 3. yat-casched.c（主调度类实现）

在 `kernel/sched/yat-casched.c` 中，实现了调度类的所有必要回调函数，初始版本均为空实现：

```c
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
```

## 4. 头文件的修改

### 4.1 include/linux/sched/yat-casched.h

新增调度器相关结构体定义：

```c
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
```

### 4.2 kernel/sched/sched.h

在 `kernel/sched/sched.h` 的 `struct rq` 结构体内部（通常与其它调度器队列如 cfs、rt 并列的位置）添加 Yat-CASched 的运行队列：

```c
#ifdef CONFIG_SCHED_YAT_CASCHED
    struct yatcasched_rq yatcasched;
#endif
```

### 4.3 include/linux/sched.h

在 `include/linux/sched.h` 的 `struct task_struct` 结构体内部（与其它调度器私有数据成员并列）添加 Yat-CASched 的私有数据：

```c
#ifdef CONFIG_SCHED_YAT_CASCHED
    struct yatcasched_task yatcasched;
#endif
```

### 4.4 调度类声明

在 `kernel/sched/sched.h` 文件的调度类声明区域（通常靠近其它 `extern const struct sched_class ...` 声明的位置）添加 Yat-CASched 的调度类声明：

```c
#ifdef CONFIG_SCHED_YAT_CASCHED
extern const struct sched_class yatcasched_sched_class;
#endif
```

### 4.5 Makefile 和 Kconfig 配置

#### kernel/sched/Makefile

在 `kernel/sched/Makefile` 中添加如下内容，使 Yat-CASched 调度器能被编译进内核：

```makefile
obj-$(CONFIG_SCHED_YAT_CASCHED) += yat-casched.o
```

#### kernel/Kconfig.preempt

在 Linux 6.13.2 中，调度器相关的配置选项位于 `kernel/Kconfig.preempt` 文件。请在该文件中添加如下配置选项，使其能在 menuconfig 中选择：

```kconfig
config SCHED_YAT_CASCHED
    bool "Yat-CASched Cache-Aware Scheduler Class"
    default n
    help
      Enable the Yat-CASched custom scheduler class.
```

---

这样，Yat-CASched 调度器的所有集成步骤就完整了。

> **说明：**
> - 如果你只是重新编译内核（未更改 initramfs 内容），可以直接用 QEMU 启动，无需重新打包 initramfs。
> - 只有在更改了 initramfs 里的文件（如 busybox、init 脚本、测试程序等）时，才需要重新打包。

后续将继续记录如何为 Yat-CASched 分配调度策略号并支持用户态切换。
