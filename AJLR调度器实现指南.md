# AJLR 缓存感知调度器实现指南：core.c 文件修改

本文档主要说明在 Linux 6.13.2 内核中实现 AJLR (Adaptive Job-Level Reclaiming) 缓存感知调度器时，需要对 `kernel/sched/core.c` 文件进行的具体修改。

## 实现步骤

在 Linux 6.13.2 内核的 `kernel/sched/core.c` 文件中需要进行两处主要修改：

### 第一处修改：在 `sched_init()` 函数中添加 AJLR 运行队列初始化

查找 `sched_init()` 函数中类似如下代码段：

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
init_dl_rq(&rq->dl);
#ifdef CONFIG_SCHED_AJLR
init_ajlr_rq(&rq->ajlr);
#endif
```

### 第二处修改：在 `sched_init()` 函数开头添加调度类检查检查

在 `sched_init()` 函数的开头部分，通常有一系列的 `BUG_ON()` 检查，用于确保调度类的层次结构正确。需要修改这部分代码来添加对 AJLR 调度类位置的检查：

```c
void __init sched_init(void)
{
    unsigned long ptr = 0;
    int i;

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
    
    // ... 其余的 sched_init 代码 ...
}
```

## 注意事项

在修改 `core.c` 文件时，需要注意以下几点：

1. **调度类定义位置**：实际上，调度类的定义（如 `DEFINE_SCHED_CLASS_HIGHEST()` 和 `DEFINE_SCHED_CLASS()`）通常不是直接在 `core.c` 中定义的，而是在 `sched.h` 或其他相关头文件中。因此，您可能需要先找到这些定义所在的文件，然后修改调度类链接顺序，确保 AJLR 调度类位于 RT 和 FAIR 调度类之间。

2. **函数实现保持一致性**：确保所有的修改与内核现有代码风格保持一致，特别是在添加条件编译（`#ifdef CONFIG_SCHED_AJLR`）部分时。

3. **初始化顺序**：在添加 AJLR 运行队列初始化时，确保它在正确的位置添加，通常是在其他调度类运行队列初始化之后，特别是 RT 和 DL 运行队列之后。

4. **错误检查**：添加调度类结构检查（`BUG_ON()`）时，遵循内核的现有模式，确保检查是完整和正确的。

5. **编译时优化**：注意在条件编译中，`CONFIG_SCHED_AJLR` 应该被定义在 `kernel/sched/Kconfig` 中。

## 总结

以上是在 Linux 6.13.2 内核的 `kernel/sched/core.c` 文件中添加 AJLR 调度器支持所需的两处主要修改。这些修改确保了 AJLR 调度类能够正确初始化并在调度类层次结构中处于适当位置（RT 和 FAIR 调度类之间）。

## 关键要点

1. **调度类定义位置**：调度类定义可能在一个单独的头文件中，而不是直接在 core.c 中
2. **调度类链接**：确保 AJLR 调度类正确链接在 RT 和 FAIR 调度类之间
3. **运行队列初始化**：确保在初始化时为每个 CPU 正确初始化 AJLR 运行队列
4. **配置项**：确保 Kconfig 中的配置选项正确依赖于 SMP（因为缓存调度主要在多处理器系统中有意义）

## 特别说明

本实现基于 Linux 6.13.2 内核，其中 RT-preempt（实时抢占）补丁已被合并到主线内核中，这为实现具有强实时性能的 AJLR 调度器提供了坚实基础。