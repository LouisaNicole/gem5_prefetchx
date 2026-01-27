#ifndef __MEM_CACHE_PREFETCH_XPT_HH__
#define __MEM_CACHE_PREFETCH_XPT_HH__

#include <vector>
#include "mem/cache/prefetch/queued.hh"
#include "params/XptPrefetcher.hh"

namespace gem5
{
namespace prefetch
{

class XptPrefetcher : public Queued
{
  protected:
    struct XptEntry {
        Addr paddr;        // 物理页地址标签
        uint32_t asid;     // 模拟地址空间 ID
        uint32_t coreId;   // 模拟核心 ID
        int missCounter;   // 32次LLC未命中阈值计数
        Tick lastAccess;   // LRU 替换时间戳
        bool enabled;      // XPT 条目激活状态
    };

    const int numEntries;
    const int threshold;
    const bool enableDefense;
    const bool isVGLO;
    
    std::vector<XptEntry> table;

    int findEntry(Addr page_addr);
    void performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id);
    void performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id);

  public:
    XptPrefetcher(const XptPrefetcherParams &p);

    // v25 版本的标准虚函数签名
    void calculatePrefetch(const PrefetchInfo &pfi, 
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif