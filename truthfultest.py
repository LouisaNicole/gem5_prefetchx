import csv
import os
import re
import subprocess
import time
from datetime import datetime

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

# ================= 实验配置 =================
GEM5_BINARY = "./build/x86/gem5.opt"
CONFIG_SCRIPT = "./configs/run_cross_core.py"
C_BINARY = "./two_core_test"  # 编译好的 C 可执行程序
OUTPUT_CSV = "batch_results_all_modes.csv"
HEATMAP_BASE = "heatmap_results"  # 生成图片的前缀

# 攻击配置：您的 C 代码打印顺序是 bit7->bit0
# 绘图时为了直观，我们保存和显示 bit7->bit0，
# 只有在 imshow() 时考虑是否需要反转纵轴。
# 这里的 awk 逻辑会遍历所有 Round，计算每个 bit 的平均延迟
# 这里的 awk 逻辑会遍历所有 Round，计算每个 bit 的平均延迟
# 用于抓取 C 程序打印的 LAT_DATA 数据的正则
LAT_DATA_PATTERN = re.compile(r"LAT_DATA:(\d+,\d+,\d+,\d+,\d+,\d+,\d+,\d+)")

# =================开始实验 =================
print(
    f"================ Batch Heatmap Experiment Started at {datetime.now()} ================"
)

# 用于绘图的矩阵 (row=key, col=bit)，每个模式一个
latency_matrices = {
    "Baseline": np.zeros((256, 8), dtype=float),
    "vID": np.zeros((256, 8), dtype=float),
    "vGLO": np.zeros((256, 8), dtype=float),
}

# 准备 CSV 文件：跑一个存一个，防止丢失数据
file_exists = os.path.isfile(OUTPUT_CSV)
if not file_exists:
    with open(OUTPUT_CSV, mode="w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            ["Target Key", "Mode", "Bit Position", "Probe Latency"]
        )

start_time = time.time()

# 循环 256 个密钥 (0x00 到 0xff)
# 循环 256 个密钥 (0x00 到 0xff)
for key_val in range(256):
    key_hex = f"0x{key_val:02x}"

    print(
        f"[{key_val+1:03d}/256] Running for Target Key: {key_hex}...",
        flush=True,
    )

    # 一个密钥，三种模式的测量
    # 一个密钥，三种模式的测量
    modes_to_test = {
        "Baseline": "--defense=0",
        "vID": "--defense=1 --mode=vID",  # 核心修正：这里要把 threshold 传给 C 吗？
        "vGLO": "--defense=1 --mode=vGLO",
    }

    for mode_name, gem5_flags in modes_to_test.items():
        # 构建 gem5 运行命令
        # 注意：这里假设您的 configs/run_cross_core.py 支持将 cmd 的参数传给 C 程序
        cmd = f"{GEM5_BINARY} {CONFIG_SCRIPT} {gem5_flags} --attacker-bin='{C_BINARY} {key_hex}'"

        # ADD：在终端实时看到
        print(f"    Running Mode: {mode_name}... ", end="", flush=True)

        try:
            # 运行 gem5，捕获所有输出
            result = subprocess.run(
                cmd,
                shell=True,
                check=True,
                capture_output=True,
                text=True,
            )
            gem5_output = result.stdout

            # 解析输出
            match = LAT_DATA_PATTERN.search(gem5_output)
            if match:
                # 提取 8 个延迟值，形如 "461,402,402,461,..."，顺序是 bit7->bit0
                latencies_str = match.group(1)
                latencies = [float(l) for l in latencies_str.split(",")]

                # 立即将结果保存到 CSV 文件：实现跑一个存一个
                # 立即将结果保存到 CSV 文件：实现跑一个存一个
                with open(OUTPUT_CSV, mode="a", newline="") as f:
                    writer = csv.writer(f)
                    for bit_idx, lat in enumerate(latencies):
                        # 保存 Target Key, Mode, Bit Position (从0到7), Probe Latency
                        # C 打印是 bit7-bit0，所以我们需要正确映射
                        bit_pos = 7 - bit_idx
                        writer.writerow([key_val, mode_name, bit_pos, lat])
                    f.flush()  # 核心修正：实时记录刷新文件数据

                # 更新绘图矩阵 (row=key, col=bit7...bit0)
                # 绘图时为了 imshow 坐标方便，我们保存 bit0 到 bit7 顺序
                latencies_reversed = list(reversed(latencies))
                latency_matrices[mode_name][key_val, :] = latencies_reversed

                # ADD：让你实时看到
                # 简单快速判定一下：如果 bit7 的延迟 > 阈值(如430)，打印 '1'，否则 '0'
                # 这里的 awk 逻辑会遍历所有 Round，计算每个 bit 的平均延迟
                guess = "".join(["1" if x > 430 else "0" for x in latencies])
                print(
                    f"Done. avg_lat: {np.mean(latencies):.1f} | 探测: {guess}"
                )
            else:
                print(f" FAIL: Could not find LAT_DATA in gem5 output.")

        except subprocess.CalledProcessError as e:
            print(f" FAIL: gem5 crashed.")
            continue

# =================保存数据分析 =================
print(f"\n[*] Experiment finished in {time.time() - start_time:.1f} seconds.")
print(f"[*] Raw data saved to: {OUTPUT_CSV}")

# =================绘制延迟热力图 =================
print(f"[*] Plotting Heatmaps...")

for mode_name, matrix in latency_matrices.items():
    plt.figure(figsize=(10, 8))

    # 绘制热力图：x轴是 bit 位置，y轴是目标密钥
    # imshow() 的 aspect='auto' 自动调整纵横比，origin='lower' 让 key=0 在下方
    plt.imshow(matrix, aspect="auto", cmap="viridis", origin="lower")

    # 设置轴标签
    plt.xlabel("Bit Position (0-7)")
    plt.ylabel("Target Key (Decimal)")
    plt.title(f"Probe Latency Heatmap across all Keys (Mode: {mode_name})")

    # 设置刻度
    plt.xticks(range(8))
    plt.yticks(range(0, 257, 16))  # 每 16 个密钥画一个刻度

    # 添加颜色条
    cbar = plt.colorbar()
    cbar.set_label("Probe Latency (cycles)")

    # 保存图片
    plot_file = f"{HEATMAP_BASE}_{mode_name}.png"
    plt.savefig(plot_file, dpi=300)
    plt.close()  # 关闭画布，准备绘制下一张
    print(f"[*] Heatmap saved to: {plot_file}")

print("================ Batch Heatmap Experiment Ended ================")
