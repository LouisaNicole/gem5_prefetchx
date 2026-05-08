#!/bin/bash

LOG_FILE="full_experiment_256.log"
CSV_FILE="experiment_results.csv" # 额外存一份CSV方便后续画图
rm -f $LOG_FILE $CSV_FILE

# 初始化 CSV 表头
echo "Target_Key,Mode,Threshold,Recovered,Status" > $CSV_FILE

echo "================ Experiment Started at $(date) ================" | tee -a $LOG_FILE

# 判定状态的辅助函数
check_status() {
    local mode=$1
    local recovered=$2
    local target=$3
    if [ "$mode" == "Baseline" ]; then
        [ "$recovered" == "$target" ] && echo "SUCCESS" || echo "FAIL"
    else
        [ "$recovered" != "$target" ] && echo "BLOCK (Pass)" || echo "LEAK (Fail)"
    fi
}

run_stage() {
    local mode=$1
    local defense=$2
    local thresh=$3
    local tmp_log="tmp_exec.log"

    # 执行 gem5
    if [ "$mode" == "Baseline" ]; then
        ./build/X86/gem5.opt ./configs/run_prefetchx_merge.py --defense=0 > $tmp_log 2>&1
    else
        ./build/X86/gem5.opt ./configs/run_prefetchx_merge.py --defense=1 --mode=$mode --threshold=$thresh > $tmp_log 2>&1
    fi

    # 提取关键信息
    local cur_key=$(grep "Recovered Key:" $tmp_log | tail -n 1 | sed -E 's/.*Recovered Key: (0x[0-9a-fA-F]+).*/\1/')
    local cur_target=$(grep "Target Key:" $tmp_log | tail -n 1 | sed -E 's/.*Target Key: (0x[0-9a-fA-F]+).*/\1/')
    local cur_thresh=$(grep "RESULT_THRESHOLD:" $tmp_log | tail -n 1 | cut -d':' -f2)

    # 如果没抓到数据，给个默认值防止脚本崩溃
    [ -z "$cur_key" ] && cur_key="N/A"
    [ -z "$cur_target" ] && cur_target="N/A"
    [ -z "$cur_thresh" ] && cur_thresh="0"

    # 导出到全局变量供汇总表使用
    eval "${mode}_KEY='$cur_key'"
    eval "${mode}_TARGET='$cur_target'"
    eval "${mode}_THRESH='$cur_thresh'"

    rm $tmp_log
}

# --- 主循环：从 00 到 FF ---
for i in {0..255}; do
    CURRENT_HEX=$(printf "0x%02x" $i)
    echo -e "\n>>> Testing Key: $CURRENT_HEX ($((i+1))/256)" | tee -a $LOG_FILE

    # 1. 动态编译当前 Key
    # 使用 -D 注入宏，这样不用改 C 代码文件
    gcc -O0 -DSECRET_KEY=$CURRENT_HEX test.c -o test

    # 2. 运行 Baseline 获取阈值
    run_stage "Baseline" 0 ""

    # 3. 运行防御模式 (使用刚才拿到的 Baseline_THRESH)
    run_stage "vID" 1 "$Baseline_THRESH"
    run_stage "vGLO" 1 "$Baseline_THRESH"

    # 4. 打印汇总表到日志和屏幕
    {
        echo "----------------------------------------------------------------"
        echo "| Mode     | Threshold | Target Key | Recovered | Status       |"
        echo "----------------------------------------------------------------"

        status_b=$(check_status "Baseline" "$Baseline_KEY" "$Baseline_TARGET")
        printf "| Baseline | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$Baseline_TARGET" "$Baseline_KEY" "$status_b"

        status_v=$(check_status "vID" "$vID_KEY" "$vID_TARGET")
        printf "| vID      | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$vID_TARGET" "$vID_KEY" "$status_v"

        status_g=$(check_status "vGLO" "$vGLO_KEY" "$vGLO_TARGET")
        printf "| vGLO     | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$vGLO_TARGET" "$vGLO_KEY" "$status_g"
        echo "----------------------------------------------------------------"
    } | tee -a $LOG_FILE

    # 5. 存入 CSV 方便以后写论文画图
    echo "$CURRENT_HEX,Baseline,$Baseline_THRESH,$Baseline_KEY,$status_b" >> $CSV_FILE
    echo "$CURRENT_HEX,vID,$Baseline_THRESH,$vID_KEY,$status_v" >> $CSV_FILE
    echo "$CURRENT_HEX,vGLO,$Baseline_THRESH,$vGLO_KEY,$status_g" >> $CSV_FILE

done

echo -e "\n================ All Experiments Ended at $(date) ================" | tee -a $LOG_FILE
