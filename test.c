#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define XPT_SIZE 256
#define KEY_BITS 8
#define EVICT_SIZE (64 * 1024) 

uint8_t memory[2048 * 4096] __attribute__((aligned(4096)));
uint8_t evict_set[EVICT_SIZE] __attribute__((aligned(4096)));

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    __asm__ volatile ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    volatile uint8_t junk;
    uint8_t secret_key = 0xd3; // 目标：11010011
    uint8_t guessed_key = 0;
    uint32_t latencies[KEY_BITS];

    memset(memory, 0, sizeof(memory));

    printf("\n" + (memory[0]*0)); // 预热
    printf("==================================================\n");
    printf("   XPT Side-Channel Attack: RSA Key Recovery      \n");
    printf("==================================================\n");

    for (int bit = 0; bit < KEY_BITS; bit++) {
        // 1. Prime (Training)
        for (int p = 0; p < XPT_SIZE; p++) {
            memory[p * 4096] = 1;
        }

        // 2. Victim Action (Conditional Eviction)
        if ((secret_key >> bit) & 1) {
            for (int p = 1024; p < 1024 + 512; p++) {
                junk = memory[p * 4096];
            }
        }

        // 3. Probe (Measurement)
        for (int i = 0; i < EVICT_SIZE; i += 64) junk = evict_set[i];
        
        __asm__ volatile("mfence");
        uint64_t start = rdtsc();
        __asm__ volatile("movb (%1), %%al" : "=a"(junk) : "r"(&memory[0]));
        uint64_t end = rdtscp(); 
        
        latencies[bit] = (uint32_t)(end - start);
    }

    // 可视化输出与逻辑判定
    printf("%-8s | %-10s | %-15s | %-6s\n", "Bit", "Latency", "Signal Graph", "Guess");
    printf("--------------------------------------------------\n");

    for (int i = 0; i < KEY_BITS; i++) {
        int guess = (latencies[i] > 330) ? 1 : 0; // 这里的 200 是根据你 298/112 的结果定的阈值
        if (guess) guessed_key |= (1 << i);

        printf("Bit %d    | %-10u | ", i, latencies[i]);
        
        // 简易图形化：长条代表高延迟（信号 1），短条代表低延迟（信号 0）
        if (guess) printf("[##########]    | 1\n");
        else       printf("[###       ]    | 0\n");
    }

    printf("--------------------------------------------------\n");
    printf("Target Secret:  0x%02x\n", secret_key);
    printf("Recovered Key: 0x%02x\n", guessed_key);
    printf("Status: %s\n", (secret_key == guessed_key) ? "✅ SUCCESS" : "❌ FAILURE");
    printf("==================================================\n\n");

    return 0;
}