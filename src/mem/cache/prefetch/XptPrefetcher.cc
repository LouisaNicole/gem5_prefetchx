#include "mem/cache/prefetch/XptPrefetcher.hh"
#include "base/random.hh" 

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
    // 1. 打印警告用于调试，确认 L3 是否收到了请求
    // warn("XPT: Request received, Addr: %#x, hasPC: %d", pfi.getAddr(), pfi.hasPC());

    // 2. 物理页地址（4KiB 对齐）作为索引
    Addr page_addr = pfi.getAddr() & ~0xFFF;
    
    // 3. 安全地获取 PC。如果 L3 拿不到 PC，则用地址位模拟 PC 
    Addr pc = pfi.hasPC() ? pfi.getPC() : (pfi.getAddr() >> 6); 
    
    // 模拟 ASID/CoreID (用于 XPTGuard 区分流)
    uint32_t simulated_id = (uint32_t)((pc >> 12) ^ (pc & 0xFFF));
    uint32_t asid = simulated_id; 
    uint32_t core_id = simulated_id; 

    // 4. 查找是否存在条目
    int idx = findEntry(page_addr);

    if (idx != -1) {
        // --- 命中 XPT 条目 ---
        table[idx].lastAccess = curTick(); 
        if (table[idx].enabled) {
            // 已激活：发出预取请求
            // 增加一条偏移地址，比如当前地址 + 64 字节
            addresses.push_back(AddrPriority(pfi.getAddr() + 64, 0));
        } else {
            // 训练中：累加 LLC 未命中计数
            table[idx].missCounter++;
            if (table[idx].missCounter >= threshold) {
                table[idx].enabled = true;
            }
        }
    } else {
        // --- 未命中 XPT：执行插入逻辑 ---
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
        table.erase(table.begin() + victim);
    }
    table.push_back({page_addr, asid, core_id, 1, curTick(), false});
}

void XptPrefetcher::performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id) {
    double c = (double)table.size();
    double n = (double)numEntries;

    static std::mt19937 gen(std::random_device{}()); 
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    double rand_val = dis(gen); // 生成 0.0 到 1.0 之间的随机数
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
        if (victim != -1) table.erase(table.begin() + victim);
    } else if (table.size() >= (size_t)numEntries) {
        table.erase(table.begin()); 
    }
    table.push_back({page_addr, asid, core_id, 1, curTick(), false});
}

int XptPrefetcher::findEntry(Addr p) {
    for (int i = 0; i < (int)table.size(); i++) if (table[i].paddr == p) return i;
    return -1;
}

} // namespace prefetch
} // namespace gem5