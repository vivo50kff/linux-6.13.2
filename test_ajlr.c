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