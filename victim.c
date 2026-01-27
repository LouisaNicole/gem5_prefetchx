#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define SECRET_KEY 0xd3
#define KEY_BITS 8
// 访问 512 个不同的页，每个页只读几次，但总量要大
#define V_STRIDE 4096 
#define V_PAGES 512

uint8_t victim_mem[V_PAGES * V_STRIDE] __attribute__((aligned(4096)));

int main() {
    volatile uint8_t junk;
    uint8_t key = SECRET_KEY;

    for (int bit = 0; bit < KEY_BITS; bit++) {
        while (access("prime_done", F_OK) == -1);

        if ((key >> bit) & 1) {
            printf("[Victim] Bit %d is 1: FORCE FLOODING XPT...\n", bit);
            // 核心逻辑：访问大量不同的物理页，强制产生 LLC Miss
            for (int p = 0; p < V_PAGES; p++) {
                junk = victim_mem[p * V_STRIDE]; 
                // 这里的 junk 访问会穿透到 L3，因为每个页面都是第一次见
            }
        } else {
            printf("[Victim] Bit %d is 0: Idle.\n", bit);
            // 适当延时，确保同步
            for (volatile int i = 0; i < 2000000; i++); 
        }
        fflush(stdout);

        close(open("victim_done", O_CREAT | O_RDWR, 0666));
        while (access("prime_done", F_OK) == 0);
    }
    return 0;
}