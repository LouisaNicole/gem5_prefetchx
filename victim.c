#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

// 受害者只访问一个全新的页面，这个页面地址必须不在攻击者的训练范围内
uint8_t victim_mem[4096] __attribute__((aligned(4096)));

int main() {
    volatile uint8_t junk;
    uint8_t key = 0xd3; 

    for (int bit = 0; bit < 8; bit++) {
        while (access("prime_done", F_OK) == -1);

        if ((key >> bit) & 1) {
            printf("[Victim] Bit %d is 1: Evicting Attacker's oldest entry (Page 0)...\n", bit);
            // 只访问一个新页，产生一个 XPT 插入，根据 LRU 逻辑必踢 Page 0
            for (int i = 0; i < 100; i++) junk = victim_mem[0];
        } else {
            printf("[Victim] Bit %d is 0: Doing nothing.\n", bit);
        }
        fflush(stdout);

        close(open("victim_done", O_CREAT | O_RDWR, 0666));
        while (access("prime_done", F_OK) == 0);
    }
    return 0;
}