import os
import re

import pandas as pd

# 配置
benchmarks = [
    "blackscholes",
    "bodytrack",
    "canneal",
    "dedup",
    "facesim",
    "ferret",
    "fluidanimate",
    "freqmine",
    "raytrace",
    "streamcluster",
    "swaptions",
    "vips",
    "x264test",
]
modes = ["base", "id", "glo"]
# 假设你的目录结构是 /path/to/parsec/blackscholes_base/stats.txt
root_path = "./"
output_csv = "simulation_results.csv"


def extract_val(content, pattern_str):
    """通用提取函数，支持提取数字并处理不存在的情况"""
    match = re.search(pattern_str, content)
    if match:
        return float(match.group(1))
    return 0.0


raw_data = []

print("开始提取数据...")

for bench in benchmarks:
    for mode in modes:
        folder_name = f"{bench}_{mode}"
        file_path = os.path.join(root_path, folder_name, "stats.txt")

        if not os.path.exists(file_path):
            print(f"跳过: {folder_name} (找不到 stats.txt)")
            continue

        with open(file_path) as f:
            content = f.read()

            # 1. 提取多核 IPC (sum of switch cores)
            # 查找所有 board.processor.switchX.core.ipc 的值
            ipc_matches = re.findall(
                r"board\.processor\.switch\d+\.core\.ipc\s+([\d\.]+)", content
            )
            total_ipc = sum(float(x) for x in ipc_matches if x != "nan")

            # 2. 提取 L3 总延迟 (Tick)
            l3_lat_total = extract_val(
                content, r"board\.l3cache\.overallMissLatency::total\s+(\d+)"
            )

            # 3. 提取 XPT 核心统计量
            xpt_hits = extract_val(
                content, r"board\.l3cache\.prefetcher\.totalXptHits\s+(\d+)"
            )
            xpt_guarded = extract_val(
                content, r"board\.l3cache\.prefetcher\.guardedAccesses\s+(\d+)"
            )
            xpt_probes = extract_val(
                content, r"board\.l3cache\.prefetcher\.totalXptProbes\s+(\d+)"
            )
            xpt_evicts = extract_val(
                content, r"board\.l3cache\.prefetcher\.evictionCount\s+(\d+)"
            )

            raw_data.append(
                {
                    "Benchmark": bench,
                    "Mode": mode,
                    "Total_IPC": total_ipc,
                    "L3_Latency_Ticks": l3_lat_total,
                    "XPT_Hits": xpt_hits,
                    "XPT_Guarded": xpt_guarded,
                    "XPT_Probes": xpt_probes,
                    "XPT_Evicts": xpt_evicts,
                }
            )

# 保存到文件
df = pd.DataFrame(raw_data)
df.to_csv(output_csv, index=False)
print(f"数据提取完毕！已保存至 {output_csv}")
