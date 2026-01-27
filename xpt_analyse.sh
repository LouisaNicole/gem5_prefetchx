#!/bin/bash

LOG_FILE=$1

if [ -z "$LOG_FILE" ]; then
    echo "使用方法: ./xpt_analyse.sh <gem5_log_file>"
    exit 1
fi

echo "======================================================"
echo "          XPT 侧信道攻击日志深度分析                 "
echo "======================================================"

# 1. 统计训练情况
TOTAL_TRAIN=$(grep -c "XPT TRAINING" $LOG_FILE)
TOTAL_ENABLED=$(grep -c "XPT STATUS: .* is now ENABLED" $LOG_FILE)

echo "[1. 训练统计]"
echo "- XPT 收到训练请求次数: $TOTAL_TRAIN"
echo "- 成功激活 (Enabled) 的条目数: $TOTAL_ENABLED"

# 2. 捕捉驱逐事件（最核心的部分）
echo -e "\n[2. 驱逐行为检测 (谁踢掉了谁)]"
# 查找所有的 EVICT 日志，并过滤出物理页地址
grep "XPT EVICT" $LOG_FILE | tail -n 20 # 只显示最近的20条，防止刷屏

# 3. 统计受害者产生的干扰
# 假设受害者的地址范围在某个特定区间，或者直接看冲突频率
CONFLICT_COUNT=$(grep "XPT EVICT" $LOG_FILE | wc -l)
echo -e "\n- 总驱逐冲突次数: $CONFLICT_COUNT"

# 4. 确认预取加速是否真的发出了
echo -e "\n[3. 硬件预取动作]"
ACTION_COUNT=$(grep -c "XPT ACTION" $LOG_FILE)
echo "- 硬件发出 Prefetch 请求次数: $ACTION_COUNT"

if [ "$ACTION_COUNT" -eq 0 ]; then
    echo "💡 警告：预取器从未发出请求，难怪 Latency 维持在 160+！检查训练强度。"
fi
echo "======================================================"