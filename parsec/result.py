import matplotlib.pyplot as plt
import numpy as np

# ================= 1. 配置原始数据 =================
# 顺序均为 [Base, vID, vGLO]
# 注：Microbench 数据取自你提供的 cpu0.ipc 和 overallMissLatency::total
ipc_raw = {
    'Canneal (S)': [0.047363, 0.050306, 0.050315],
    'Canneal (L)': [0.047363, 0.047365, 0.047362],
    'Blackscholes (L)': [0.238322, 0.238320, 0.238322],
    'Fluidanimate (L)': [0.245544, 0.245548, 0.245542],
    'Microbench': [0.046331, 0.046285, 0.046312]  # 新增数据
}

lat_raw = {
    'Canneal (S)': [462428318457, 462466283454, 462428351424],
    'Canneal (L)': [3602999013381, 3602955427011, 3603156114456],
    'Blackscholes (L)': [231871925637, 231895439433, 231871925637],
    'Fluidanimate (L)': [1109414505969, 1109261868759, 1109424873258],
    'Microbench': [94809347748, 94839012387, 94870192842] # 新增数据
}

hit_raw = {
    'Canneal (S)': [2455, 2457, 2454],
    'Canneal (L)': [2929, 2766, 2726],
    'Blackscholes (L)': [1390167, 1390505, 1390161],
    'Fluidanimate (L)': [208154, 207099, 208111],
    'Microbench': [188236, 187456, 187179] # 新增数据
}

# ================= 2. 核心计算函数 =================
def calculate_metrics(wl_list):
    res = {'name': [], 'id_deg': [], 'glo_deg': [], 'id_inf': [], 'glo_inf': [], 'id_surv': [], 'glo_surv': []}
    for name in wl_list:
        res['name'].append(name)
        # IPC 衰减: (1 - 防御/Base) * 100
        res['id_deg'].append((1 - ipc_raw[name][1]/ipc_raw[name][0]) * 100)
        res['glo_deg'].append((1 - ipc_raw[name][2]/ipc_raw[name][0]) * 100)
        # 延迟增加: (防御/Base - 1) * 100
        res['id_inf'].append((lat_raw[name][1]/lat_raw[name][0] - 1) * 100)
        res['glo_inf'].append((lat_raw[name][2]/lat_raw[name][0] - 1) * 100)
        # 命中存活率: (防御/Base) * 100
        res['id_surv'].append((hit_raw[name][1]/hit_raw[name][0]) * 100)
        res['glo_surv'].append((hit_raw[name][2]/hit_raw[name][0]) * 100)
    return res

# ================= 3. 绘图逻辑 =================
def create_plot(wl_names, filename, title_prefix):
    data = calculate_metrics(wl_names)
    x = np.arange(len(wl_names))
    width = 0.35

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(20, 6))
    fig.suptitle(f'XPTGuard Security & Performance: {title_prefix}', fontsize=16, fontweight='bold')

    def add_labels(ax, rects):
        for rect in rects:
            height = rect.get_height()
            ax.annotate(f'{height:.3f}%',
                        xy=(rect.get_x() + rect.get_width() / 2, height),
                        xytext=(0, 3), textcoords="offset points",
                        ha='center', va='bottom', fontsize=9, fontweight='bold')

    # Plot 1: IPC Degradation
    r1 = ax1.bar(x - width/2, data['id_deg'], width, label='vID', color='#3498db', edgecolor='black', alpha=0.8)
    r2 = ax1.bar(x + width/2, data['glo_deg'], width, label='vGLO', color='#e67e22', edgecolor='black', alpha=0.8)
    ax1.set_title('IPC Degradation (%)', fontsize=12)
    ax1.set_ylabel('Lower is Better')
    ax1.set_xticks(x)
    ax1.set_xticklabels(wl_names)
    ax1.axhline(0, color='black', linewidth=1)
    add_labels(ax1, r1); add_labels(ax1, r2)

    # Plot 2: Latency Inflation
    r3 = ax2.bar(x - width/2, data['id_inf'], width, label='vID', color='#3498db', edgecolor='black', alpha=0.8)
    r4 = ax2.bar(x + width/2, data['glo_inf'], width, label='vGLO', color='#e67e22', edgecolor='black', alpha=0.8)
    ax2.set_title('LLC Miss Latency Inflation (%)', fontsize=12)
    ax2.set_ylabel('Lower is Better')
    ax2.set_xticks(x)
    ax2.set_xticklabels(wl_names)
    ax2.axhline(0, color='black', linewidth=1)
    add_labels(ax2, r3); add_labels(ax2, r4)

    # Plot 3: Hit Survival Rate
    r5 = ax3.bar(x - width/2, data['id_surv'], width, label='vID', color='#3498db', edgecolor='black', alpha=0.8)
    r6 = ax3.bar(x + width/2, data['glo_surv'], width, label='vGLO', color='#e67e22', edgecolor='black', alpha=0.8)
    ax3.set_title('XPT Hit Survival Rate (%)', fontsize=12)
    ax3.set_ylabel('Closer to 100% is Better')
    ax3.set_xticks(x)
    ax3.set_xticklabels(wl_names)
    # 动态调整y轴范围以便看清差异
    ymin = min(data['id_surv'] + data['glo_surv']) - 2
    ax3.set_ylim(max(0, ymin), 105)
    ax3.axhline(100, color='red', linestyle='--', alpha=0.5)
    add_labels(ax3, r5); add_labels(ax3, r6)

    for ax in [ax1, ax2, ax3]:
        ax.legend(loc='best')
        ax.grid(axis='y', linestyle=':', alpha=0.5)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    plt.savefig(filename, dpi=300)
    print(f"成功生成图表: {filename}")

# ================= 4. 执行分组绘图 =================
group_canneal = ['Canneal (S)', 'Canneal (L)']
group_others = ['Blackscholes (L)', 'Fluidanimate (L)', 'Microbench']

create_plot(group_canneal, 'canneal_scale_comparison.png', 'Canneal Scale Comparison')
create_plot(group_others, 'other_workloads_comparison.png', 'General Workloads & Microbench')