#include "mem/cache/prefetch/XptPrefetcher.hh"
#include "debug/HWPrefetch.hh"

namespace gem5 {
namespace prefetch {

XptPrefetcher::XptPrefetcher(const XptPrefetcherParams &p)
    : Queued(p),
      gen(std::random_device{}()),     // 1. gen 在前
      numEntries(p.num_entries),       // 2. numEntries
      threshold(p.activation_threshold),
      isVGLO(p.is_vGLO),               // 3. isVGLO
      enableDefense(p.enable_defense), // 4. enableDefense
      xptStats(this)                   // 5. xptStats 最后 (因为在 .hh 的类末尾)
{
    table.reserve(numEntries);
}

XptPrefetcher::XptStats::XptStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(totalXptHits, statistics::units::Count::get(), "Total XPT logical hits"),
      ADD_STAT(guardedAccesses, statistics::units::Count::get(), "Blocked by XPTGuard"),
      ADD_STAT(totalXptProbes, statistics::units::Count::get(), "Total number of Probes into XPT table"),
      ADD_STAT(evictionCount, statistics::units::Count::get(), "Total number of evictions from XPT table")
{
    safetyInterventionRate
        .name("safety_intervention_rate") 
        .desc("Percentage of hits blocked for security");
    
    safetyInterventionRate = guardedAccesses / totalXptHits;

    effectiveSpeedupRate
        .name("effective_speedup_rate")
        .desc("Percentage of hits that actually received bypass");

    effectiveSpeedupRate = (totalXptHits - guardedAccesses) / totalXptHits;
}

uint32_t
XptPrefetcher::getEntryOwnerId(Addr addr) const
{
    Addr page_addr = addr & ~0xFFF;
    for (const auto& entry : table) {
        if (entry.paddr == page_addr) {
            return entry.asid; // 返回该页表项绑定的 Context ID
        }
    }
    return 0; // 默认值
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
    xptStats.totalXptProbes++;

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
    DPRINTF(HWPrefetch, "=== XPTBaseline === page=0x%lx asid=%u core=%u\n", page_addr, asid, core_id);
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
        // 记录一次驱逐
        xptStats.evictionCount++;
        DPRINTF(HWPrefetch, "  [EVICT] idx=%d page=0x%lx enabled=%d missCnt=%d lastAccess=%ld\n",
                victim, table[victim].paddr, table[victim].enabled, 
                table[victim].missCounter, table[victim].lastAccess);
        table.erase(table.begin() + victim);
    }
    // 记录一次插入
    // xptStats.totalXptProbes++;
    table.push_back({page_addr, asid, core_id, 0, curTick(), false});
}

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    double c = (double)table.size();
    double n = (double)numEntries;

    static std::mt19937 gen(std::random_device{}()); 
    std::uniform_int_distribution<int> dis(0, 255);
    int rand_x = dis(gen);
    // double rand_val = 0.4;
    // double max_prob = 0.2;
    // double prob = (c/n) * max_prob;
    // double prob = 1.0 - pow(1.0 - (c/n), 2.0);
    double prob = (c/n);
    int threshold = static_cast<int>(prob * 256);
    // std::cerr << "rand_val: " << rand_val << " prob: " << prob << std::endl;

    DPRINTF(HWPrefetch, "=== XPTGuard === page=0x%lx asid=%u core=%u c=%d/n=%d P=%d r=%d trig=%s vGLO=%d\n",
            page_addr, asid, core_id, (int)c, numEntries, threshold, rand_x, 
            (rand_x <= threshold ? "YES" : "NO"), isVGLO);

    int victim = -1;
    if (c > 0 && rand_x <= threshold) {
        xptStats.guardedAccesses++; // 💡 记录这一次随机驱逐
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
                    DPRINTF(HWPrefetch, "vID fallback to Global LRU eviction, page=0x%lx\n", table[victim].paddr);
                    table.erase(table.begin() + victim);
                }
                DPRINTF(HWPrefetch, "vID [INSERT] page=0x%lx -> table size: %d/%d\n", page_addr, (int)table.size()+1, numEntries);
                table.push_back({page_addr, asid, core_id, 0, curTick(), false});
                // c<n: victim=-1，直接插入空闲槽（不驱逐）
            }
            else {
                DPRINTF(HWPrefetch, "vID [PROB-EVICT] same ID, page=0x%lx\n", table[victim].paddr);
                table.erase(table.begin() + victim);
                DPRINTF(HWPrefetch, "vID [INSERT] page=0x%lx -> table size: %d/%d\n", page_addr, (int)table.size()+1, numEntries);
                table.push_back({page_addr, asid, core_id, 0, curTick(), false});
            }
        } else {
            // vGLO: 全局LRU驱逐最旧条目
            victim = 0;
            for (int i = 1; i < (int)table.size(); i++) {
                if (table[i].lastAccess < table[victim].lastAccess)
                    victim = i;
            }
            DPRINTF(HWPrefetch, "vGLO [PROB-EVICT], page=0x%lx\n", table[victim].paddr);
            table.erase(table.begin() + victim);
            DPRINTF(HWPrefetch, "vGLO [INSERT] page=0x%lx -> table size: %d/%d\n", page_addr, (int)table.size()+1, numEntries);
            table.push_back({page_addr, asid, core_id, 0, curTick(), false});
        }
    }
    // r > P: 不触发概率置换，直接插入
    // 如果表满且未触发概率置换，则回退全局LRU
    else {
        if (table.size() >= numEntries) {
            xptStats.evictionCount++; // 记录正常满载驱逐
            victim = 0;
            for (int i = 1; i < (int)table.size(); i++) {
                if (table[i].lastAccess < table[victim].lastAccess)
                    victim = i;
            }
            DPRINTF(HWPrefetch, "[NO-PROB-EVICT] fallback to Global LRU eviction.\n");
            table.erase(table.begin() + victim);
        }
        DPRINTF(HWPrefetch, "  [INSERT] page=0x%lx -> table size: %d/%d\n", page_addr, (int)table.size()+1, numEntries);
        table.push_back({page_addr, asid, core_id, 0, curTick(), false});
    }
    // 最后统一记录插入
    // xptStats.totalXptProbes++;
    DPRINTF(HWPrefetch, "=== XPTGuard END ===\n");
}

} // namespace prefetch
} // namespace gem5