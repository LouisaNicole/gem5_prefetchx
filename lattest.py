import matplotlib.pyplot as plt
import numpy as np

# Use a standard style for academic papers
plt.style.use("ggplot")
plt.rcParams["font.family"] = "serif"

# Experimental Data (Target Key: 0x96 -> 10010110 in binary)
# Mapping: Bit 0 to Bit 7
bits = np.arange(8)
# Baseline medians based on your log: 1 -> 461, 0 -> 402
# Key 0x96 binary (LSB to MSB): 0, 1, 1, 0, 1, 0, 0, 1
baseline_medians = [402, 451, 451, 402, 451, 402, 402, 451]
defense_medians = [451] * 8

fig, ax = plt.subplots(figsize=(10, 6))

# 1. Plot Baseline: Red dashed line with large markers
ax.plot(
    bits,
    baseline_medians,
    marker="o",
    markersize=10,
    markerfacecolor="white",
    markeredgewidth=2,
    color="#E24A33",
    linewidth=2.5,
    linestyle="--",
    label="Baseline (Unprotected)",
)

# 2. Plot XPTGuard: Blue thick solid line with square markers
ax.plot(
    bits,
    defense_medians,
    marker="s",
    markersize=8,
    color="#348ABD",
    linewidth=4,
    alpha=0.7,
    label="XPTGuard (vID/vGLO)",
)

# 3. Plot Threshold Reference
ax.axhline(
    y=427, color="#467821", linestyle=":", linewidth=2, label="Threshold (427)"
)

# 4. Final Decorations
ax.set_xticks(bits)
ax.set_xticklabels([f"Bit {i}" for i in bits])
ax.set_xlabel("Key Bit Position", fontsize=12)
ax.set_ylabel("Median Probe Latency (Cycles)", fontsize=12)
ax.set_title(
    "Comparison of Side-Channel Latency (Target Key: 0x96)",
    fontsize=14,
    pad=20,
)
ax.set_ylim(360, 500)

# Annotate Bit values on top of Baseline markers
for i, lat in enumerate(baseline_medians):
    val = "1" if lat > 427 else "0"
    ax.text(i, lat + 8, val, ha="center", color="#E24A33", fontweight="bold")

ax.legend(loc="lower right", frameon=True, facecolor="white")
ax.grid(axis="y", linestyle="--", alpha=0.5)

plt.tight_layout()
# Save as a high-quality PNG for your thesis
plt.savefig("xpt_attack_results_en.png", dpi=300)
print("Plot saved as xpt_attack_results_en.png")
plt.show()
