# QEMU 启动 Linux 内核完整流程

## test_labroom 目录下操作流程

先进入目录：
```bash
cd /home/lwd/桌面/test_labroom/linux-6.13.2
```

1. 编译内核，获得 bzImage

```bash
make -j$(nproc)
```

2. 构建极简 initramfs（假设已在 test_labroom/linux-6.13.2 目录下）

```bash
mkdir -p test_initramfs/{bin,dev,proc,sys}
cp /bin/busybox test_initramfs/bin/
ln -sf busybox test_initramfs/bin/sh
cat > test_initramfs/init << EOF
#!/bin/sh
exec /bin/sh
EOF
chmod +x test_initramfs/init
sudo mknod -m 666 test_initramfs/dev/null c 1 3  # 创建 /dev/null 设备节点，避免 shell 重定向报错
cd test_initramfs
find . | cpio -o -H newc | gzip > ../test_initramfs.cpio.gz
cd ..
```

> 说明：必须用 mknod 创建 /dev/null，否则 shell 下如 `yes > /dev/null &` 会报错。

3. 启动 QEMU 运行内核

```bash
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd test_initramfs.cpio.gz \
  -append "console=ttyS0 init=/init selinux=0" \
  -serial stdio \
  -m 1G \
  -smp 1
```

进入 shell 即为成功。

---

## linux-6.13.2（含 AJLR 代码）目录下操作流程

先进入目录：
```bash
cd /home/lwd/桌面/linux-6.13.2
```

1. 编译内核，获得 bzImage

```bash
make -j$(nproc)
```

2. 构建极简 initramfs（同上，可复用 test_labroom 生成的 test_initramfs.cpio.gz）

3. 启动 QEMU 运行内核

```bash
qemu-system-x86_64 -kernel arch/x86/boot/bzImage \
  -initrd test_initramfs.cpio.gz \
  -append "console=ttyS0 init=/init selinux=0" \
  -serial stdio \
  -m 1G \
  -smp 1
```

进入 shell 即为成功。

---

## AJLR 调度器调试流程

如果 AJLR 版本内核启动时出现 page fault 或 panic，按以下步骤排查：

### 1. 最小化 AJLR 实现

先让所有 AJLR 函数只 return，不做实际操作：

```c
// 在 kernel/sched/ajlr.c 中
static void enqueue_task_ajlr(struct rq *rq, struct task_struct *p, int flags) { return; }
static void dequeue_task_ajlr(struct rq *rq, struct task_struct *p, int flags) { return; }
static struct task_struct *pick_next_task_ajlr(struct rq *rq) { return NULL; }
static void check_preempt_curr_ajlr(struct rq *rq, struct task_struct *p, int flags) { return; }
```

### 2. 检查 sched_init() 初始化

确保 AJLR 初始化代码在 `kernel/sched/core.c` 的 sched_init() 中正确添加：

```c
for_each_possible_cpu(i) {
    struct rq *rq = cpu_rq(i);
    // ...existing code...
    init_cfs_rq(&rq->cfs);
    init_rt_rq(&rq->rt);
    init_dl_rq(&rq->dl);
#ifdef CONFIG_SCHED_AJLR
    init_ajlr_rq(&rq->ajlr);  // 确保此函数正确实现
#endif
    // ...existing code...
}
```

### 3. 逐步测试

- 编译最小化版本，确认能进 shell
- 逐步恢复 AJLR 功能，每次只加一个函数的实际逻辑
- 每次修改后重新编译测试

### 4. 添加调试信息

在关键函数中添加 printk：

```c
static void init_ajlr_rq(struct ajlr_rq *ajlr_rq)
{
    printk("AJLR: Initializing AJLR runqueue\n");
    // 初始化代码
}
```

### 5. 检查结构体定义

确保 `struct ajlr_rq` 和相关结构体正确定义，所有指针初始化为 NULL。

---

## 重新配置所有 AJLR 修改的步骤

如果重新使用了空白内核，需要按以下顺序重新做所有修改：

### 1. 创建头文件
```bash
# 已创建 include/linux/sched/ajlr.h
```

### 2. 修改 Kconfig
```bash
# 已在 kernel/Kconfig.preempt 末尾添加 AJLR 配置
```

### 3. 修改 sched.h
```bash
# 已在 task_struct 中添加 ajlr 字段
```

### 4. 接下来需要的修改

还需要修改以下文件：

- 修改 `kernel/sched/sched.h` 添加 `struct ajlr_rq ajlr;` 到 `struct rq`
- 修改 `kernel/sched/core.c` 添加 sched_init() 初始化
- 修改 `kernel/sched/Makefile` 添加编译规则
- 使用最小化的 `kernel/sched/ajlr.c`

### 5. 配置和编译
```bash
make menuconfig  # 启用 AJLR 选项
make -j$(nproc)  # 编译
```