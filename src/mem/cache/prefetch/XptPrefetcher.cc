#include "mem/cache/prefetch/XptPrefetcher.hh"
#include "base/random.hh" 
#include "debug/HWPrefetch.hh" // 使用标准 Debug Flag

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

void XptPrefetcher::calculatePrefetch(const PrefetchInfo &pfi, 
                                 std::vector<AddrPriority> &addresses,
                                 const CacheAccessor &cache) 
{
    // 1. 获取物理页地址 (4KB 对齐)
    Addr page_addr = pfi.getAddr() & ~0xFFF;
    
    // 简单模拟 ID：在 SE 模式下很难获取真实 ASID，这里用 PC 模拟
    Addr pc = pfi.hasPC() ? pfi.getPC() : 0;
    uint32_t simulated_id = (uint32_t)((pc >> 12) ^ (pc & 0xFF));
    uint32_t asid = simulated_id; 
    uint32_t core_id = simulated_id; 

    // 2. 查找是否存在条目
    int idx = findEntry(page_addr);

    if (idx != -1) {
        // --- XPT HIT ---
        // 更新 LRU 时间戳
        table[idx].lastAccess = curTick(); 
        
        DPRINTF(HWPrefetch, "XPT HIT: Page=%#x, Cnt=%d, En=%d\n", 
                page_addr, table[idx].missCounter, table[idx].enabled);

        if (table[idx].enabled) {
            // 已激活：发出预取请求 (模拟 Optimized LLC Miss)
            // 预取下一行 (Current + 64)
            // Addr pf_addr = pfi.getAddr() + 64;
            // 检查跨页
            // if ((pf_addr & ~0xFFF) == page_addr) {
            //     addresses.push_back(AddrPriority(pf_addr, 0));
            //     DPRINTF(HWPrefetch, "XPT PREFETCH: Issuing %#x\n", pf_addr);
            // }
        } else {
            // 未激活：训练阶段
            table[idx].missCounter++;
            if (table[idx].missCounter >= threshold) {
                table[idx].enabled = true;
                DPRINTF(HWPrefetch, "XPT ACTIVATE: Page=%#x now enabled\n", page_addr);
            }
        }
    } else {
        // --- XPT MISS: 插入新条目 ---
        DPRINTF(HWPrefetch, "XPT MISS: Page=%#x, Inserting...\n", page_addr);
        if (enableDefense) {
            performXPTGuard(page_addr, asid, core_id);
        } else {
            performBaselineInsert(page_addr, asid, core_id);
        }
    }
}

void XptPrefetcher::performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id) {
    // 只有表满时才驱逐
    if (table.size() >= (size_t)numEntries) {
        int victim = 0;
        // 寻找 lastAccess 最小的 (最旧的)
        for (int i = 1; i < (int)table.size(); i++) {
            if (table[i].lastAccess < table[victim].lastAccess) {
                victim = i;
            }
        }
        
        DPRINTF(HWPrefetch, "XPT EVICT: Removing Page=%#x (LRU)\n", table[victim].paddr);
        table.erase(table.begin() + victim);
    }
    
    // 插入新条目，计数器初始化为 1
    XptEntry new_entry;
    new_entry.paddr = page_addr;
    new_entry.asid = asid;
    new_entry.coreId = core_id;
    new_entry.missCounter = 1;
    new_entry.lastAccess = curTick();
    new_entry.enabled = false;
    
    table.push_back(new_entry);
}

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    // 简化的 Guard 逻辑，为了攻击复现，这里暂时保持简单
    performBaselineInsert(page_addr, asid, core_id);
}

int XptPrefetcher::findEntry(Addr p) {
    for (int i = 0; i < (int)table.size(); i++) {
        if (table[i].paddr == p) return i;
    }
    return -1;
}

} // namespace prefetch
} // namespace gem5