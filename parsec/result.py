import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

# 1. 加载数据
df = pd.read_csv("simulation_results.csv")

# 2. 预处理数据
final_list = []
for bench in df["Benchmark"].unique():
    subset = df[df["Benchmark"] == bench].copy()
    base_row = subset[subset["Mode"] == "base"]
    if base_row.empty:
        continue

    base_ipc = base_row.iloc[0]["Total_IPC"]
    base_lat = base_row.iloc[0]["L3_Latency_Ticks"]

    subset["IPC_Speedup_Pct"] = (
        (subset["Total_IPC"] - base_ipc) / base_ipc * 100
    )
    # 注意：这里改成了 Change，正数代表延迟增加（变慢）
    subset["Lat_Change_Pct"] = (
        (subset["L3_Latency_Ticks"] - base_lat) / base_lat * 100
    )
    subset["Efficiency_Ratio"] = (
        subset["XPT_Hits"] - subset["XPT_Guarded"]
    ) / subset["XPT_Hits"].replace(0, 1)
    subset["Intervention_Density"] = subset["XPT_Guarded"] / subset[
        "XPT_Probes"
    ].replace(0, 1)
    final_list.append(subset)

plot_df = pd.concat(final_list)
plot_df = plot_df[plot_df["Mode"] != "base"]

# 统一文本框样式 (Wheat, semi-transparent)
formula_props = dict(boxstyle="round", facecolor="wheat", alpha=0.3)


def annotate_bars(ax, fmt="%.3f"):
    for container in ax.containers:
        ax.bar_label(container, fmt=fmt, padding=3, fontsize=9)


# --- 绘图 1: 性能分析 (嵌入公式) ---
sns.set_theme(style="whitegrid", palette="muted")
fig1, (ax1, ax2) = plt.subplots(2, 1, figsize=(15, 12))

# 1-A: IPC Speedup
sns.barplot(
    data=plot_df, x="Benchmark", y="IPC_Speedup_Pct", hue="Mode", ax=ax1
)
ax1.set_title("IPC Speedup over Baseline (%)", fontsize=15)
ax1.axhline(0, color="black", lw=1)
# 嵌入 IPC 公式
ax1.text(
    0.02,
    0.95,
    r"$IPC\ Speedup\ (\%) = \frac{Total\_IPC_{defense} - Total\_IPC_{base}}{Total\_IPC_{base}} \times 100\%$",
    transform=ax1.transAxes,
    fontsize=12,
    verticalalignment="top",
    bbox=formula_props,
)
annotate_bars(ax1, fmt="%.3f%%")

# 1-B: Latency Change (正数代表 slower)
sns.barplot(
    data=plot_df, x="Benchmark", y="Lat_Change_Pct", hue="Mode", ax=ax2
)
ax2.set_title(
    "L3 Miss Latency Change (%) - Positive means Slower", fontsize=15
)
ax2.axhline(0, color="black", lw=1)
# 嵌入 Latency 公式
ax2.text(
    0.02,
    0.95,
    r"$Lat\ Change\ (\%) = \frac{Latency\ Ticks_{defense} - Latency\ Ticks_{base}}{Latency\ Ticks_{base}} \times 100\%$",
    transform=ax2.transAxes,
    fontsize=12,
    verticalalignment="top",
    bbox=formula_props,
)
annotate_bars(ax2, fmt="%.3f%%")

plt.xticks(rotation=45)
plt.tight_layout()
plt.savefig("performance_analysis.png")

# --- 绘图 2: 防御分析 (已嵌入公式，截断 canneal 数据以看清细节) ---
fig2, (ax3, ax4) = plt.subplots(2, 1, figsize=(15, 14))

# 2-A: Efficiency Ratio
sns.barplot(
    data=plot_df, x="Benchmark", y="Efficiency_Ratio", hue="Mode", ax=ax3
)
ax3.set_title("Bypass Efficiency Ratio", fontsize=15)
ax3.set_ylim(-2, 1.5)  # 关键：截断坐标轴，让其他数据清晰
# 嵌入 Ratio 公式
ax3.text(
    0.02,
    0.95,
    r"$Efficiency\ Ratio = \frac{XPT\_Hits - XPT\_Guarded}{XPT\_Hits}$",
    transform=ax3.transAxes,
    fontsize=14,
    verticalalignment="top",
    bbox=formula_props,
)
annotate_bars(ax3, fmt="%.3f")

# 2-B: Intervention Density
sns.barplot(
    data=plot_df, x="Benchmark", y="Intervention_Density", hue="Mode", ax=ax4
)
ax4.set_title("Intervention Density (Security Strength)", fontsize=15)
# 嵌入 Density 公式
ax4.text(
    0.02,
    0.95,
    r"$Intervention\ Density = \frac{XPT\_Guarded}{XPT\_Probes}$",
    transform=ax4.transAxes,
    fontsize=14,
    verticalalignment="top",
    bbox=formula_props,
)
annotate_bars(ax4, fmt="%.3f")

plt.xticks(rotation=45)
plt.tight_layout()
plt.savefig("defense_efficiency_analysis.png")

print("分析完成：")
print("1. performance_analysis.png (带 IPC/Latency 公式)")
print(
    "2. defense_efficiency_analysis.png (带 Ratio/Density 公式，已处理 canneal 极值)"
)
