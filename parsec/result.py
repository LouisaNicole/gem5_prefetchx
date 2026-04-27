import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

# 1. 加载与预处理
df = pd.read_csv("simulation_results.csv")
final_list = []
for bench in df["Benchmark"].unique():
    subset = df[df["Benchmark"] == bench].copy()
    base_row = subset[subset["Mode"] == "base"]
    if base_row.empty: continue
    base_ipc = base_row.iloc[0]["Total_IPC"]
    base_lat = base_row.iloc[0]["L3_Latency_Ticks"]
    subset["IPC_Speedup_Pct"] = ((subset["Total_IPC"] - base_ipc) / base_ipc * 100)
    subset["Lat_Change_Pct"] = ((subset["L3_Latency_Ticks"] - base_lat) / base_lat * 100)
    final_list.append(subset)
plot_df = pd.concat(final_list)
plot_df = plot_df[plot_df["Mode"] != "base"]

# --- 2. 核心样式配置 (参考图风格：大标题、大公式、紧凑布局) ---
TITLE_SIZE = 24
LABEL_SIZE = 18
TICK_SIZE = 14
BAR_LABEL_SIZE = 11
FORMULA_SIZE = 19 # 再次调大公式字号

formula_props = dict(boxstyle="round,pad=0.5", facecolor="wheat", alpha=0.2, edgecolor="none")
sns.set_theme(style="whitegrid", palette="Set2")

# ==========================================
# --- 绘制 IPC 图表 ---
# ==========================================
fig, ax = plt.subplots(figsize=(14, 7.5)) # 稍微调高比例

sns.barplot(data=plot_df, x="Benchmark", y="IPC_Speedup_Pct", hue="Mode", ax=ax)

# 标题与轴标签
ax.set_title("System Throughput: IPC Speedup over Baseline (%)", fontsize=TITLE_SIZE, pad=45, fontweight='bold')
ax.set_ylabel("IPC Speedup (%)", fontsize=LABEL_SIZE)
ax.set_xlabel("Benchmarks", fontsize=LABEL_SIZE)
ax.axhline(0, color="black", lw=1.5)

# --- 关键改进 1: 极大化顶部留白，防止公式重叠 ---
y_max = plot_df["IPC_Speedup_Pct"].max()
y_min = plot_df["IPC_Speedup_Pct"].min()
ax.set_ylim(y_min - 1, y_max * 1.6) # 留出 60% 的顶部空间

# --- 关键改进 2: 公式水平居中 (x=0.5)，位置更高 (y=0.98) ---
ipc_formula = r"$IPC\ Speedup\ (\%) = \frac{Total\_IPC_{defense} - Total\_IPC_{base}}{Total\_IPC_{base}} \times 100\%$"
ax.text(0.5, 0.98, ipc_formula, transform=ax.transAxes, fontsize=FORMULA_SIZE, 
         verticalalignment="top", horizontalalignment="center", bbox=formula_props)

# 柱头标注
for container in ax.containers:
    ax.bar_label(container, fmt="%.2f%%", padding=5, fontsize=BAR_LABEL_SIZE, fontweight='bold')

plt.xticks(rotation=20, ha="right", fontsize=TICK_SIZE)

# --- 关键改进 3: 强制收紧底部，删除多余空白 ---
plt.tight_layout()
# 手动微调：让底部更窄，顶部给标题留位
plt.subplots_adjust(bottom=0.12, top=0.88) 

plt.savefig("performance_ipc_final_v2.png", bbox_inches='tight', dpi=300)
plt.show()

# ==========================================
# --- 绘制 Latency 图表 (同样逻辑) ---
# ==========================================
fig2, ax2 = plt.subplots(figsize=(14, 7.5))
sns.barplot(data=plot_df, x="Benchmark", y="Lat_Change_Pct", hue="Mode", ax=ax2)

ax2.set_title("Hardware Overhead: L3 Miss Latency Change (%)", fontsize=TITLE_SIZE, pad=45, fontweight='bold')
ax2.set_ylabel("Latency Change (%)", fontsize=LABEL_SIZE)
ax2.set_xlabel("Benchmarks", fontsize=LABEL_SIZE)
ax2.axhline(0, color="black", lw=1.5)

lat_max = plot_df["Lat_Change_Pct"].max()
lat_min = plot_df["Lat_Change_Pct"].min()
ax2.set_ylim(lat_min - 1, lat_max * 1.6)

lat_formula = r"$Lat\ Change\ (\%) = \frac{Latency\ Ticks_{defense} - Latency\ Ticks_{base}}{Latency\ Ticks_{base}} \times 100\%$"
ax2.text(0.5, 0.98, lat_formula, transform=ax2.transAxes, fontsize=FORMULA_SIZE, 
         verticalalignment="top", horizontalalignment="center", bbox=formula_props)

for container in ax2.containers:
    ax2.bar_label(container, fmt="%.2f%%", padding=5, fontsize=BAR_LABEL_SIZE, fontweight='bold')

plt.xticks(rotation=20, ha="right", fontsize=TICK_SIZE)
plt.tight_layout()
plt.subplots_adjust(bottom=0.12, top=0.88)

plt.savefig("performance_latency_final_v2.png", bbox_inches='tight', dpi=300)