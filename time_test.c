// 编译: gcc -O0 -static test.c -o test
// 运行: ./build/X86/gem5.opt --debug-flags=XPTDebug ... > log.txt

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <x86intrin.h> 

uint8_t memory[64 * 1024 * 1024]; 
static volatile uint8_t global_junk;

int main() {
    // 1. 预热 (这里会产生大量日志，包括你看到的那个 ENABLED，但那是假象！)
    printf("Pre-heating...\n");
    for (int i = 0; i < 64 * 1024 * 1024; i += 4096) memory[i] = 1; 

    // =======================================================
    // 2. 这里的 MARKER 是分界线！
    // =======================================================
    // 只有在这个标记之后出现的 ENABLED，才是真正的成功！
    
    // 先 Flush 确保 Marker 能穿透
    _mm_clflush(&memory[4096]); 
    __asm__ volatile("mfence; lfence");

    printf("\n\n");
    printf("====================================================\n");
    printf("=== REAL ATTACK STARTS HERE (IGNORE LOGS ABOVE) ====\n");
    printf("====================================================\n");
    
    // 访问 Marker (Page 1)，在日志里留个记号
    global_junk = memory[4096]; 

    // 3. 真正开始训练 Page 0
    // 我们要把它从“被驱逐”的状态救回来
    // 加大药量到 200 次！
    for (int i = 0; i < 200; i++) {
        global_junk = memory[0];
        __asm__ volatile("mfence");
        _mm_clflush(&memory[0]);
        __asm__ volatile("mfence; lfence");
        
        // 简单的延时，防止合并
        for(int k=0; k<1000; k++) __asm__ volatile("nop");
    }
    
    // 4. Probe
    // ... (你的 Probe 代码)
    
    return 0;
}