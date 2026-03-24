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
    if (victim != -1) table.erase(table.begin() + victim);
    table.push_back({page_addr, asid, core_id, 0, curTick(), false});
}

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    double c = (double)table.size();
    double n = (double)numEntries;

    static std::mt19937 gen(std::random_device{}()); 
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double rand_val = dis(gen); // 生成 0.0 到 1.0 之间的随机数
    // double rand_val = 0.4;
    double prob = 1.0 - pow(1.0 - (c/n), 2.0);
    std::cerr << "rand_val: " << rand_val << " prob: " << prob << std::endl;
    int victim = -1;
    if (c > 0 && rand_val <= prob) { // 触发概率替换
        if (!isVGLO) { // vID模式: 仅限同 ID 域
            for (int i = 0; i < table.size(); i++) {
                if (table[i].asid == asid && table[i].coreId == core_id) {
                    if (victim == -1 || table[i].lastAccess < table[victim].lastAccess) victim = i;
                }
            }
        }
        // vGLO模式或vID未找到匹配项且已满时，强制全局LRU
        if (victim == -1 && (isVGLO || table.size() >= numEntries)) {
            victim = 0;
            for (int i = 1; i < table.size(); i++) {
                if (table[i].lastAccess < table[victim].lastAccess) victim = i;
            }
        }
    } else if (table.size() >= numEntries) { // 未触发概率但满了，强制LRU
        victim = 0;
        for (int i = 1; i < table.size(); i++) {
            if (table[i].lastAccess < table[victim].lastAccess) victim = i;
        }
    }

    if (victim != -1) table.erase(table.begin() + victim);
    table.push_back({page_addr, asid, core_id, 1, curTick(), false});
}

} // namespace prefetch
} // namespace gem5