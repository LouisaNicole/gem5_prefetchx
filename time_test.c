#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>

#define PAGE_SIZE 4096
#define TRAIN_THRESHOLD 32  // 回归论文 32 次标准阈值

// 强制刷新缓存
void clflush(volatile void *p) {
    asm volatile("clflush (%0)" :: "r"(p));
}

// 精确测量计时函数
uint64_t measure_access_latency(volatile char *ptr) {
    uint64_t start, end;
    unsigned int junk;
    asm volatile("mfence");
    start = __rdtscp(&junk);
    // 产生实际读取动作，制造数据依赖
    asm volatile("movb (%1), %%al" : "=a"(junk) : "r"(ptr));
    asm volatile("mfence");
    end = __rdtscp(&junk);
    return end - start;
}

int main() {
    // 分配大块内存
    size_t size = 20 * PAGE_SIZE;
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, size);

    // 【核心改进】：点射式初始化，杜绝 memset
    // 我们只初始化每个页面的第 0 个字节。每个页面初始计数仅为 1。
    volatile char *p_hit  = &buffer[PAGE_SIZE * 1]; 
    volatile char *p_dram = &buffer[PAGE_SIZE * 5]; 
    volatile char *p_xpt  = &buffer[PAGE_SIZE * 9]; 

    *p_hit  = 0x40;
    *p_dram = 0x41;
    *p_xpt  = 0x42;

    printf("[*] Standard Threshold: %d\n", TRAIN_THRESHOLD);
    printf("[*] Addresses: Hit=%p, DRAM=%p, XPT=%p\n", p_hit, p_dram, p_xpt);

    // --- 1. 测量 LLC Hit ---
    volatile char dummy = *p_hit; // 载入缓存
    asm volatile("mfence");
    uint64_t t_hit = measure_access_latency(p_hit);

    // --- 2. 测量 DRAM Miss (此时 p_dram 访问计数为 1, 低于 32, 必交税) ---
    clflush(p_dram);
    asm volatile("mfence");
    uint64_t t_dram = measure_access_latency(p_dram);

    // --- 3. 训练 XPT (训练 50 次，稳稳超过 32) ---
    printf("[*] Training XPT for Page %p...\n", (void*)((uintptr_t)p_xpt & ~0xFFF));
    for (int i = 0; i < 50; i++) {
        volatile char junk = *p_xpt;
        clflush(p_xpt);
        asm volatile("mfence");
    }

    // 给 DRAM 控制器冷却时间，确保之前的训练写回不会干扰测量
    for(volatile int i=0; i<2000000; i++);

    // --- 4. 测量 XPT Hit (已激活，免税路径) ---
    clflush(p_xpt);
    asm volatile("mfence");
    uint64_t t_xpt = measure_access_latency(p_xpt);

    printf("\n--- Final Academic Results ---\n");
    printf("LLC Hit (Baseline)  : %lu cycles\n", t_hit);
    printf("XPT Hit (Optimized) : %lu cycles\n", t_xpt);
    printf("DRAM Miss (Taxed)   : %lu cycles\n", t_dram);
    printf("------------------------------\n");
    printf("Attacker Signal Gap : %ld cycles\n", (long)t_dram - (long)t_xpt);

    return 0;
}