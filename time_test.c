#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define XPT_SIZE 256
#define EVICT_SIZE (64 * 1024)
uint8_t memory[2048 * 4096] __attribute__((aligned(4096)));
uint8_t evict_set[EVICT_SIZE] __attribute__((aligned(4096)));

static inline uint64_t rdtsc_measure(uint8_t *addr, int clean_cache) {
    volatile uint8_t junk;
    uint64_t start, end;
    uint32_t lo, hi;

    // 如果需要测 Miss/XPT，就清理 L1/L2；如果测 LLC Hit，就不清理
    if (clean_cache) {
        for (int i = 0; i < EVICT_SIZE; i += 64) junk = evict_set[i];
    }
    
    __asm__ volatile("mfence; lfence");
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    start = ((uint64_t)hi << 32) | lo;

    junk = *addr; 

    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi));
    end = ((uint64_t)hi << 32) | lo;
    return end - start;
}

int main() {
    memset(memory, 0, sizeof(memory));
    memset(evict_set, 1, sizeof(evict_set));

    volatile uint8_t junk;
    // --- 状态 1: LLC 命中 (LLC Hit) ---
    // 先访问一次让它进缓存，且不清理 L1/L2 测量
    junk = memory[100 * 4096]; 
    uint64_t lat_llc_hit = rdtsc_measure(&memory[100 * 4096], 0);

    // --- 状态 2: 优化型未命中 (XPT Hit/Enabled) ---
    // 训练 Page 0
    for (int i = 0; i < 40; i++) memory[0] = i; 
    uint64_t lat_xpt_hit = rdtsc_measure(&memory[0], 1);

    // --- 状态 3: 完全未命中 (LLC Miss / XPT Miss) ---
    // 训练后被驱逐，或者直接测从未访问过的页
    uint64_t lat_llc_miss = rdtsc_measure(&memory[500 * 4096], 1);

    printf("\n论文指标对齐报告:\n");
    printf("--------------------------------------------------\n");
    printf("1. LLC 命中 (LLC Hit):       %lu 周期 (预期 <160)\n", lat_llc_hit);
    printf("2. 优化型未命中 (XPT Enabled): %lu 周期 (预期 170-330)\n", lat_xpt_hit);
    printf("3. 完全未命中 (LLC Miss):    %lu 周期 (预期 >350)\n", lat_llc_miss);
    printf("--------------------------------------------------\n");
    
    return 0;
}