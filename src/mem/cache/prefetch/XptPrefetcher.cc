#include "mem/cache/prefetch/XptPrefetcher.hh"
#include "base/random.hh" 
#include "debug/XPTDebug.hh"

namespace gem5
{
namespace prefetch
{

XptPrefetcher::XptPrefetcher(const XptPrefetcherParams &p)
    : Queued(p), 
      numEntries(p.num_entries), 
      threshold(p.activation_threshold),
      enableDefense(p.enable_defense),
      isVGLO(p.is_vGLO)
{
    table.reserve(numEntries);
}

void 
XptPrefetcher::calculatePrefetch(const PrefetchInfo &pfi, 
                                 std::vector<AddrPriority> &addresses,
                                 const CacheAccessor &cache) 
{
    // 1. 获取基础信息
    Addr page_addr = pfi.getAddr() & ~0xFFF;
    Addr pc = pfi.hasPC() ? pfi.getPC() : (pfi.getAddr() >> 6); 
    
    // 模拟 ASID/CoreID
    uint32_t simulated_id = (uint32_t)((pc >> 12) ^ (pc & 0xFFF));
    uint32_t asid = simulated_id; 
    uint32_t core_id = simulated_id; 

    // 2. 查找是否存在条目
    int idx = findEntry(page_addr);

    if (idx != -1) {
        // --- 命中 XPT 条目 ---
        DPRINTF(XPTDebug, "XPT Lookup HIT: PageAddr=%#x, Enabled=%d, MissCounter=%d\n", 
                page_addr, table[idx].enabled, table[idx].missCounter);
        
        table[idx].lastAccess = curTick(); 
        if (table[idx].enabled) {
            DPRINTF(XPTDebug, "XPT ACTION: Issuing prefetch for Addr=%#x\n", pfi.getAddr() + 64);
            addresses.push_back(AddrPriority(pfi.getAddr() + 64, 0));
        } else {
            table[idx].missCounter++;
            DPRINTF(XPTDebug, "XPT TRAINING: PageAddr=%#x missCounter=%d/%d\n", 
                    page_addr, table[idx].missCounter, threshold);
            if (table[idx].missCounter >= threshold) {
                table[idx].enabled = true;
                DPRINTF(XPTDebug, "XPT STATUS: PageAddr=%#x is now ENABLED\n", page_addr);
            }
        }
    } else {
        // --- 未命中 XPT：执行插入逻辑 ---
        DPRINTF(XPTDebug, "XPT Lookup MISS: PageAddr=%#x, initiating insert.\n", page_addr);
        if (enableDefense) {
            performXPTGuard(page_addr, asid, core_id);
        } else {
            performBaselineInsert(page_addr, asid, core_id);
        }
    }
}

void XptPrefetcher::performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id) {
    if (table.size() >= (size_t)numEntries) {
        int victim = 0;
        for (int i = 1; i < (int)table.size(); i++) {
            if (table[i].lastAccess < table[victim].lastAccess) victim = i;
        }
        
        // 关键 Debug：谁被驱逐了
        DPRINTF(XPTDebug, "XPT EVICT (Baseline): Removing %#x to make room for %#x\n", 
                table[victim].paddr, page_addr);
        
        table.erase(table.begin() + victim);
    }
    DPRINTF(XPTDebug, "XPT INSERT: PageAddr=%#x\n", page_addr);
    table.push_back({page_addr, asid, core_id, 1, curTick(), false});
}

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    double c = (double)table.size();
    double n = (double)numEntries;

    static std::mt19937 gen(std::random_device{}()); 
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double rand_val = dis(gen); 

    if (c > 0 && rand_val <= (c / n)) {
        int victim = -1;
        Tick oldest = curTick();

        if (!isVGLO) { // vID 模式
            for (int i = 0; i < (int)table.size(); i++) {
                if (table[i].asid == asid && table[i].coreId == core_id) {
                    if (table[i].lastAccess < oldest) { oldest = table[i].lastAccess; victim = i; }
                }
            }
        }
        
        if (victim == -1) { 
            for (int i = 0; i < (int)table.size(); i++) {
                if (table[i].lastAccess < oldest) { oldest = table[i].lastAccess; victim = i; }
            }
        }
        
        if (victim != -1) {
            DPRINTF(XPTDebug, "XPT EVICT (Guard): Removing %#x due to random eviction\n", table[victim].paddr);
            table.erase(table.begin() + victim);
        }
    } else if (table.size() >= (size_t)numEntries) {
        DPRINTF(XPTDebug, "XPT EVICT (Guard): Table full, removing head for %#x\n", page_addr);
        table.erase(table.begin()); 
    }
    
    DPRINTF(XPTDebug, "XPT INSERT (Guard): PageAddr=%#x\n", page_addr);
    table.push_back({page_addr, asid, core_id, 1, curTick(), false});
}

int XptPrefetcher::findEntry(Addr p) {
    for (int i = 0; i < (int)table.size(); i++) if (table[i].paddr == p) return i;
    return -1;
}

} // namespace prefetch
} // namespace gem5