#include "mem/cache/prefetch/XptPrefetcher.hh"
#include "debug/HWPrefetch.hh"

namespace gem5 {
namespace prefetch {

XptPrefetcher::XptPrefetcher(const XptPrefetcherParams &p)
    : Queued(p), numEntries(p.num_entries), threshold(p.activation_threshold),
      enableDefense(p.enable_defense), isVGLO(p.is_vGLO), gen(std::random_device{}())
{
    table.reserve(numEntries);
}

bool XptPrefetcher::isXptOptimizedHit(Addr addr) const
{
    Addr page_addr = addr & ~0xFFF;
    for (const auto& entry : table) {
        // 必须是物理页匹配，且已经过了 32 次训练（enabled == true）
        if (entry.paddr == page_addr && entry.enabled) {
            return true; 
        }
    }
    return false;
}

int XptPrefetcher::findEntry(Addr page_addr) {
    for (int i = 0; i < table.size(); i++) {
        if (table[i].paddr == page_addr) return i;
    }
    return -1;
}

void XptPrefetcher::notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi) {
    if (acc.pkt && acc.pkt->req->hasContextId()) {
        current_asid = acc.pkt->req->contextId();
        // CoreID 从 requestorId 获取，对应哪个 CPU 发出的请求
        current_core = acc.pkt->req->requestorId();
    }
    Queued::notify(acc, pfi);
}

void XptPrefetcher::calculatePrefetch(const PrefetchInfo &pfi, 
                                 std::vector<AddrPriority> &addresses,
                                 const CacheAccessor &cache) {
    Addr page_addr = pfi.getAddr() & ~0xFFF;
    int idx = findEntry(page_addr);

    if (idx != -1) {
        // ✅ 记录已存在条目的更新（攻击者目标页会频繁出现在这里）
        DPRINTF(HWPrefetch, "XPT_UPDATE: page=0x%lx idx=%d enabled=%d missCnt=%d lastAccess=%ld->%ld\n",
                page_addr, idx, table[idx].enabled, table[idx].missCounter,
                table[idx].lastAccess, curTick());
        table[idx].lastAccess = curTick();
        if (!table[idx].enabled) {
            table[idx].missCounter++;
            if (table[idx].missCounter >= threshold) table[idx].enabled = true;
        }
    } else {
        if (enableDefense) performXPTGuard(page_addr, current_asid, current_core);
        else performBaselineInsert(page_addr, current_asid, current_core);
    }
}

void XptPrefetcher::performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id) {
    int victim = -1;
    // 算法1: 寻找同ASID但不同Core的条目
    for (int i = 0; i < table.size(); i++) {
        if (table[i].asid == asid && table[i].coreId != core_id) {
            if (victim == -1 || table[i].lastAccess < table[victim].lastAccess) victim = i;
        }
    }
    // 如果没有，且满了，执行全局LRU
    if (victim == -1 && table.size() >= numEntries) {
        victim = 0;
        for (int i = 1; i < table.size(); i++) {
            if (table[i].lastAccess < table[victim].lastAccess) victim = i;
        }
    }
    if (victim != -1) {
        DPRINTF(HWPrefetch, "  [EVICT] idx=%d page=0x%lx enabled=%d missCnt=%d lastAccess=%ld\n",
                victim, table[victim].paddr, table[victim].enabled, 
                table[victim].missCounter, table[victim].lastAccess);
        table.erase(table.begin() + victim);
    }
    table.push_back({page_addr, asid, core_id, 0, curTick(), false});
}

// void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
//     double c = (double)table.size();
//     double n = (double)numEntries;

//     std::uniform_real_distribution<double> dis(0.0, 1.0);
//     double rand_val = dis(gen); // 生成 0.0 到 1.0 之间的随机数
//     // double prob = 1.0 - pow(1.0 - (c/n), 2.0);
//     double prob = c/n;
//     DPRINTF(HWPrefetch, "=== XPTGuard === page=0x%lx asid=%u core=%u c=%d/n=%d P=%.3f r=%.3f trig=%s vGLO=%d\n",
//             page_addr, asid, core_id, (int)c, numEntries, prob, rand_val, 
//             (rand_val <= prob ? "YES" : "NO"), isVGLO);
//     int victim = -1;
//     if (c > 0 && rand_val <= prob) {
//         victim = 0;
//         for (int i = 0; i < table.size(); i++) {
//             if (table[i].lastAccess < table[victim].lastAccess) victim = i;
//         }
//     }
//     if (victim != -1) {
//         DPRINTF(HWPrefetch, "  [EVICT] idx=%d page=0x%lx enabled=%d missCnt=%d lastAccess=%ld\n",
//                 victim, table[victim].paddr, table[victim].enabled, 
//                 table[victim].missCounter, table[victim].lastAccess);
//         table.erase(table.begin() + victim);
//     }
//     DPRINTF(HWPrefetch, "  [INSERT] page=0x%lx -> table size: %d/%d\n",
//             page_addr, (int)table.size()+1, numEntries);
//     table.push_back({page_addr, asid, core_id, 0, curTick(), false});
//     DPRINTF(HWPrefetch, "=== XPTGuard END ===\n");
// }

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    double c = (double)table.size();
    double n = (double)numEntries;

    // static std::mt19937 gen(std::random_device{}()); 
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double rand_val = dis(gen); // 生成 0.0 到 1.0 之间的随机数
    // double rand_val = 0.4;
    // double prob = c/n;
    double prob = 1.0 - pow(1.0 - (c/n), 2.0);
    // double prob = sqrt(c/n);
    // std::cerr << "rand_val: " << rand_val << " prob: " << prob << std::endl;

    DPRINTF(HWPrefetch, "=== XPTGuard === page=0x%lx asid=%u core=%u c=%d/n=%d P=%.3f r=%.3f trig=%s vGLO=%d\n",
            page_addr, asid, core_id, (int)c, numEntries, prob, rand_val, 
            (rand_val <= prob ? "YES" : "NO"), isVGLO);

    int victim = -1;
    if (c > 0 && rand_val <= prob) {
        // 触发概率置换
        if (!isVGLO) {
            // vID: 找同(ASID, CoreID)最旧条目
            for (int i = 0; i < (int)table.size(); i++) {
                if (table[i].asid == asid && table[i].coreId == core_id) {
                    if (victim == -1 || table[i].lastAccess < table[victim].lastAccess)
                        victim = i;
                }
            }
            // 找不到同ID条目
            if (victim == -1) {
                if (table.size() >= numEntries) {
                    // c==n: 回退全局LRU
                    victim = 0;
                    for (int i = 1; i < (int)table.size(); i++) {
                        if (table[i].lastAccess < table[victim].lastAccess)
                            victim = i;
                    }
                }
                // c<n: victim=-1，直接插入空闲槽（不驱逐）
            }
        } else {
            // vGLO: 全局LRU驱逐最旧条目
            victim = 0;
            for (int i = 1; i < (int)table.size(); i++) {
                if (table[i].lastAccess < table[victim].lastAccess)
                    victim = i;
            }
            DPRINTF(HWPrefetch, "XPTGuard: page=0x%lx c=%d n=%d P=%.3f r=%.3f trig=%d\n",
            page_addr, (int)c, numEntries, prob, rand_val, (rand_val<=prob));
        }
        // ✅ 关键3：记录最终驱逐的条目
        if (victim != -1) {
            DPRINTF(HWPrefetch, "  [EVICT] idx=%d page=0x%lx enabled=%d missCnt=%d lastAccess=%ld\n",
                    victim, table[victim].paddr, table[victim].enabled, 
                    table[victim].missCounter, table[victim].lastAccess);
            table.erase(table.begin() + victim);
        }
    }
    // r > P: 不触发概率置换，直接插入
    // 如果表满且未触发概率置换，则本次请求不插入（丢弃）
    else {
        if (table.size() >= numEntries) {
            return;
        }
    }
    DPRINTF(HWPrefetch, "  [INSERT] page=0x%lx -> table size: %d/%d\n",
            page_addr, (int)table.size()+1, numEntries);
    table.push_back({page_addr, asid, core_id, 0, curTick(), false});
    DPRINTF(HWPrefetch, "=== XPTGuard END ===\n");
}

} // namespace prefetch
} // namespace gem5