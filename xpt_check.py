import re

def analyze_cache_health(file_path):
    with open(file_path, 'r') as f:
        content = f.read()

    # 提取关键指标
    stats = {
        "L3_Replacements": r"system.l3cache.replacements\s+(\d+)",
        "L3_Misses": r"system.l3cache.overallMisses::total\s+(\d+)",
        "PF_Useful": r"system.l3cache.prefetcher.pfUseful\s+(\d+)",
        "MSHR_Misses": r"system.l3cache.overallMshrMisses::l3cache.prefetcher\s+(\d+)"
    }

    res = {}
    for k, v in stats.items():
        match = re.search(v, content)
        res[k] = int(match.group(1)) if match else 0

    # 计算攻击强度比率
    # 如果物理替换次数远高于预取有效次数，说明环境极其拥挤
    collision_index = res["L3_Replacements"] / (res["PF_Useful"] + 1)

    print("="*45)
    print(f"{'硬件统计项':<25} | {'检测数值':<10}")
    print("-" * 45)
    print(f"{'L3 物理替换 (驱逐)':<25} | {res['L3_Replacements']}")
    print(f"{'L3 总未命中 (Miss)':<25} | {res['L3_Misses']}")
    print(f"{'XPT 预取命中 (Useful)':<25} | {res['PF_Useful']}")
    print(f"{'MSHR 阻塞次数':<25} | {res['MSHR_Misses']}")
    print("-" * 45)
    print(f"微架构拥挤度指数: {collision_index:.2f}")
    
    if collision_index > 10:
        print("💡 结论：L3 处于极度拥挤状态，驱逐效应非常明显。")
    else:
        print("💡 结论：L3 空间尚可，攻击可能更多依赖资源阻塞。")
    print("="*45)

if __name__ == "__main__":
    analyze_cache_health("m5out/stats.txt")