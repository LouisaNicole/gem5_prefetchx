#!/bin/bash

LOG_FILE="full_experiment.log"
rm -f $LOG_FILE

echo "================ Experiment Started at $(date) ================" | tee -a $LOG_FILE

# 编译
gcc -O3 test.c -o test

run_stage() {
    local mode=$1
    local defense=$2
    local thresh=$3
    local tmp_log="tmp_exec.log"

    echo -e "\n[RUNNING] Mode: $mode, Defense: $defense..." | tee -a $LOG_FILE
    
    if [ "$mode" == "Baseline" ]; then
        ./build/X86/gem5.opt ./configs/run_prefetchx.py --defense=0 > $tmp_log 2>&1
    else
        ./build/X86/gem5.opt ./configs/run_prefetchx.py --defense=1 --mode=$mode --threshold=$thresh > $tmp_log 2>&1
    fi

    cat $tmp_log >> $LOG_FILE

    # --- 关键修正：精准抓取 Key ---
    # 匹配 "Recovered Key: 0xXX" 格式，并提取 0xXX
    local cur_key=$(grep "Recovered Key:" $tmp_log | tail -n 1 | sed -E 's/.*Recovered Key: (0x[0-9a-fA-F]+).*/\1/')
    # 匹配 "Target Key: 0xYY"
    local cur_target=$(grep "Target Key:" $tmp_log | tail -n 1 | sed -E 's/.*Target Key: (0x[0-9a-fA-F]+).*/\1/')
    # 匹配 "RESULT_THRESHOLD:ZZZ"
    local cur_thresh=$(grep "RESULT_THRESHOLD:" $tmp_log | tail -n 1 | cut -d':' -f2)

    # 导出到全局变量
    eval "${mode}_KEY='$cur_key'"
    eval "${mode}_TARGET='$cur_target'"
    eval "${mode}_THRESH='$cur_thresh'"
    
    rm $tmp_log
}

# 执行流程
run_stage "Baseline" 0 ""
run_stage "vID" 1 "$Baseline_THRESH"
run_stage "vGLO" 1 "$Baseline_THRESH"

# --- 动态生成汇总表并写入日志 ---
{
    echo -e "\n"
    echo "----------------------------------------------------------------"
    echo "| Mode     | Threshold | Target Key | Recovered | Status       |"
    echo "----------------------------------------------------------------"

    # 自动判断逻辑
    check_status() {
        if [ "$1" == "$2" ]; then
            echo "LEAK (Fail)"
        else
            echo "BLOCK (Pass)"
        fi
    }

    printf "| Baseline | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$Baseline_TARGET" "$Baseline_KEY" "$([ "$Baseline_KEY" == "$Baseline_TARGET" ] && echo "SUCCESS" || echo "ERROR")"
    printf "| vID      | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$vID_TARGET" "$vID_KEY" "$(check_status "$vID_KEY" "$vID_TARGET")"
    printf "| vGLO     | %-9s | %-10s | %-9s | %-12s |\n" "$Baseline_THRESH" "$vGLO_TARGET" "$vGLO_KEY" "$(check_status "$vGLO_KEY" "$vGLO_TARGET")"
    echo "----------------------------------------------------------------"
    echo -e "\n================ Experiment Ended at $(date) ================"
} | tee -a $LOG_FILE  # 
