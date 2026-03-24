"""
XPT Prefetcher FS 仿真配置
基于官方 configs/deprecated/example/fs.py 改写
在标准系统上挂载 XptPrefetcher 到 L3 缓存
"""

import argparse
import sys
import os

import m5
from m5.defines import buildEnv
from m5.objects import *
from m5.util import addToPath, fatal, warn

# ── 路径设置 (必须在 import common 之前) ─────────────────────
# 找到 gem5 根目录下的 configs 目录
_dir = os.path.dirname(os.path.abspath(__file__))
# 如果脚本在 configs/ 下，上一级就是 gem5 根目录
addToPath(os.path.join(_dir, '..'))
addToPath(os.path.join(_dir, '../common'))

from common import Options, Simulation, CacheConfig, MemConfig, ObjectList
from common.Benchmarks import *
from common.Caches import *
from common.FSConfig import *
from common.SysPaths import *

# ── 参数 ──────────────────────────────────────────────────────
parser = argparse.ArgumentParser(description="XPT Prefetcher FS Simulation")

# 复用官方参数
Options.addCommonOptions(parser)
Options.addFSOptions(parser)

# XPT 专属参数
parser.add_argument("--xpt-entries", type=int, default=256,
                    help="XPT 表项数 (default: 256)")
parser.add_argument("--xpt-threshold", type=int, default=32,
                    help="XPT 激活阈值 (default: 32)")
parser.add_argument("--enable-defense", action="store_true",
                    help="启用 XPTGuard 防御")
parser.add_argument("--defense-mode", choices=["vID", "vGLO"], default="vID",
                    help="防御模式 (default: vID)")

args = parser.parse_args()

# ── CPU / 内存类型 ─────────────────────────────────────────────
(TestCPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(args)
TestMemClass = Simulation.setMemClass(args)

# ── 磁盘配置 ──────────────────────────────────────────────────
if args.benchmark:
    try:
        bm = [Benchmarks[args.benchmark]]
    except KeyError:
        print(f"Error: benchmark {args.benchmark} not defined.")
        sys.exit(1)
else:
    bm = [SysConfig(
        disks=args.disk_image,
        rootdev=args.root_device,
        mem=args.mem_size,
        os_type=args.os_type,
    )]

np = args.num_cpus

# ── 构建标准 x86 系统 ─────────────────────────────────────────
test_sys = makeLinuxX86System(
    test_mem_mode, np, bm[0], args.ruby, cmdline=None
)

test_sys.cache_line_size = args.cacheline_size
test_sys.voltage_domain = VoltageDomain(voltage=args.sys_voltage)
test_sys.clk_domain = SrcClockDomain(
    clock=args.sys_clock,
    voltage_domain=test_sys.voltage_domain
)
test_sys.cpu_voltage_domain = VoltageDomain()
test_sys.cpu_clk_domain = SrcClockDomain(
    clock=args.cpu_clock,
    voltage_domain=test_sys.cpu_voltage_domain
)

if args.kernel is not None:
    test_sys.workload.object_file = binary(args.kernel)

if args.script is not None:
    test_sys.readfile = args.script
    
if args.command_line:
    test_sys.workload.command_line = args.command_line
    
# test_sys.workload.command_line = "earlyprintk=ttyS0 console=ttyS0 lpj=7999923 root=/dev/hda1 ide=nodma"
test_sys.init_param = args.init_param

# CPU
test_sys.cpu = [
    TestCPUClass(clk_domain=test_sys.cpu_clk_domain, cpu_id=i)
    for i in range(np)
]
for cpu in test_sys.cpu:
    cpu.createThreads()

# ── 标准缓存配置 ──────────────────────────────────────────────
CacheConfig.config_cache(args, test_sys)
MemConfig.config_mem(args, test_sys)

# ── 在 L3 上挂载 XptPrefetcher ────────────────────────────────
# 注意: 只有在使用 --l2cache (即有 L2/L3) 时才有 l2cache 对象
# 官方脚本里 --l2cache 实际上创建的是 LLC
if hasattr(test_sys, 'l2'):
    print("[XPT] 找到 l2 cache，挂载 XptPrefetcher...")
    is_vglo = (args.defense_mode == "vGLO")
    xpt = XptPrefetcher(
        num_entries          = args.xpt_entries,
        activation_threshold = args.xpt_threshold,
        enable_defense       = args.enable_defense,
        is_vGLO              = is_vglo,
    )
    test_sys.l2.prefetcher = xpt
    print(f"[XPT] entries={args.xpt_entries}, threshold={args.xpt_threshold}")
    print(f"[XPT] defense={'ON (' + args.defense_mode + ')' if args.enable_defense else 'OFF'}")
else:
    print("[XPT] 警告: 未找到 l2 cache 对象，XptPrefetcher 未挂载")
    print("[XPT] 请确保使用了 --l2cache 参数")

# ── 启动仿真 ──────────────────────────────────────────────────
root = Root(full_system=True, system=test_sys)

Simulation.setWorkCountOptions(test_sys, args)
Simulation.run(args, root, test_sys, FutureClass)