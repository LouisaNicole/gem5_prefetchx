#ifndef __MEM_CACHE_PREFETCH_XPT_PREFETCHER_HH__
#define __MEM_CACHE_PREFETCH_XPT_PREFETCHER_HH__

#include <vector>
#include "mem/cache/prefetch/queued.hh"
#include "params/XptPrefetcher.hh"

namespace gem5
{
namespace prefetch
{

class XptPrefetcher : public Queued
{
  private: // 使用 private 封装内部结构
    struct XptEntry {
        Addr paddr;        // 物理页地址
        uint32_t asid;     // 模拟 ASID
        uint32_t coreId;   // 模拟 CoreID
        int missCounter;   // 激活计数器
        Tick lastAccess;   // LRU 时间戳
        bool enabled;      // 是否激活
    };

    const int numEntries;
    const int threshold;
    const bool enableDefense;
    const bool isVGLO;
    
    std::vector<XptEntry> table;

    // 辅助函数
    int findEntry(Addr page_addr);
    void performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id);
    void performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id);

  public:
    XptPrefetcher(const XptPrefetcherParams &p);

    // 供 Cache 调用：检查是否为 XPT 优化命中（不改变 LRU 状态）
    bool isXptOptimizedHit(Addr addr) const;

    void calculatePrefetch(const PrefetchInfo &pfi, 
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif