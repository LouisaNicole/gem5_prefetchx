#ifndef __MEM_CACHE_PREFETCH_XPT_PREFETCHER_HH__
#define __MEM_CACHE_PREFETCH_XPT_PREFETCHER_HH__

#include <vector>
#include <random>
#include "mem/cache/prefetch/queued.hh"
#include "params/XptPrefetcher.hh"
#include "base/statistics.hh"
#include "base/stats/group.hh"
#include "base/stats/units.hh"

namespace gem5
{
namespace prefetch
{

class XptPrefetcher : public Queued
{
  private:
    struct XptEntry {
        Addr paddr;        // 物理页地址 (4KB对齐)
        uint32_t asid;     // 模拟进程标识 (Context ID)
        uint32_t coreId;   // 模拟核心标识
        int missCounter;   
        Tick lastAccess;   
        bool enabled;      
    };

    std::vector<XptEntry> table;
    std::mt19937 gen; // 随机数生成器
    const int numEntries;
    const int threshold;
    const bool isVGLO;
    const bool enableDefense;

    int findEntry(Addr page_addr);
    void performBaselineInsert(Addr page_addr, uint32_t asid, uint32_t core_id);
    void performXPTGuard(Addr page_addr, uint32_t asid, uint32_t core_id);

  public:

    uint32_t getEntryOwnerId(Addr addr) const;

    XptPrefetcher(const XptPrefetcherParams &p);
    
    struct XptStats : public statistics::Group {
        XptStats(statistics::Group *parent);
        statistics::Scalar totalXptHits;
        statistics::Scalar guardedAccesses;
        // 自动计算的指标
        statistics::Formula safetyInterventionRate; // 拦截率：B/A
        statistics::Formula effectiveSpeedupRate;   // 有效加速率：(A-B)/A
    } xptStats; // 💡 变量名从 stats 改为 xptStats，避开父类冲突

    bool isXptOptimizedHit(Addr addr) const;
    void calculatePrefetch(const PrefetchInfo &pfi, 
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
    // FS模式下通过notify捕捉真实的ContextID
    void notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi) override;
    
    // 当前请求的上下文信息
    uint32_t current_asid = 0;
    uint32_t current_core = 0;

    // // Key: 物理页地址, Value: 被驱逐时的 Tick
    // std::unordered_map<Addr, Tick> evictionRegistry;
    // // 记录页面被驱逐的时间，用于静默期拦截
    // std::unordered_map<Addr, Tick> evictionDeadTime;
    // // 静默期长度：设置为 50,000,000 Ticks (约 50 微秒)，足以吃掉所有 MSHR 幽灵回填
    // const Tick DEAD_TIME_DURATION = 50000000ULL;
};

} // namespace prefetch
} // namespace gem5
#endif