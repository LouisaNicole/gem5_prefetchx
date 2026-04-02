"""
Script to run PARSEC benchmarks with gem5.
The script expects a benchmark program name and the simulation
size. The system is fixed with 2 CPU cores, MESI Two Level system
cache and 3 GiB DDR4 memory. It uses the x86 board.

This script will count the total number of instructions executed
in the ROI. It also tracks how much wallclock and simulated time.

Usage:
------
```
Baseline:
./build/X86/gem5.opt ./configs/run_parsec.py --benchmark canneal --size simmedium --defense 0   3481530890094

vID:
./build/X86/gem5.opt ./configs/run_parsec.py --benchmark canneal --size simsmall --defense 1 --mode vID

vGLO:
./build/X86/gem5.opt ./configs/run_parsec.py --benchmark blackscholes --size simsmall --defense 1 --mode vGLO
```
"""
import argparse
import time
import m5
from m5.objects import *

from gem5.components.boards.x86_board import X86Board
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_switchable_processor import SimpleSwitchableProcessor
from gem5.isas import ISA
from gem5.resources.resource import KernelResource, DiskImageResource
from gem5.simulate.exit_event import ExitEvent
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

# --- 1. 参数解析 ---
parser = argparse.ArgumentParser(description="Classic Hierarchy PARSEC with XPTGuard")
parser.add_argument("--benchmark", type=str, required=True)
parser.add_argument("--size", type=str, required=True, choices=["simsmall", "simmedium", "simlarge"])
parser.add_argument("--defense", type=int, default=0, help="1:Enable, 0:Disable")
parser.add_argument("--mode", type=str, default="vGLO", choices=["vID", "vGLO"])
args = parser.parse_args()

# 路径配置
local_disk_path = "/home/louisa/parsec-tests/prebuilt-image/parsec.img"
local_vmlinux_path = "/home/louisa/parsec-tests/vmlinux"

# --- 2. 自定义 Classic 三级缓存层级 ---
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import AbstractClassicCacheHierarchy

class XptClassicHierarchy(AbstractClassicCacheHierarchy):
    def __init__(self):
        super(XptClassicHierarchy, self).__init__()
        self.membus = SystemXBar(width=64)

    def get_mem_side_port(self):
        return self.membus.mem_side_ports

    def get_cpu_side_port(self):
        return self.membus.cpu_side_ports

    def incorporate_cache(self, board):
        # ==============================================================
        # 1. 最核心的修复：调用官方接口强制连线
        # ==============================================================
        # 显式连接内存条（彻底消灭 MemCtrl is unconnected 报错）
        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports
        
        # 显式连接系统端口（彻底消灭所有的 AttributeError，用于 IO 和内核引导）
        board.connect_system_port(self.membus.cpu_side_ports)

        # ==============================================================
        # 2. 共享 L3 缓存 (LLC)
        # ==============================================================
        board.l3bus = L2XBar()
        board.l3cache = Cache(
            size='1MiB', assoc=16, 
            tag_latency=50, data_latency=20, response_latency=20,
            mshrs=12, tgts_per_mshr=12
        )
        
        # 注入 XptPrefetcher
        board.l3cache.prefetcher = XptPrefetcher(
            num_entries = 256,
            activation_threshold = 32
        )
        board.l3cache.prefetcher.enable_defense = bool(args.defense)
        if args.defense:
            board.l3cache.prefetcher.is_vGLO = (args.mode == "vGLO")
            print(f"[*] XPTGuard Active: Mode={args.mode}")

        board.l3cache.cpu_side = board.l3bus.mem_side_ports
        board.l3cache.mem_side = self.membus.cpu_side_ports

        # ==============================================================
        # 3. 私有 L1/L2 缓存及 CPU 中断连线
        # ==============================================================
        for cpu in board.get_processor().get_cores():
            cpu.icache = Cache(size='32KiB', assoc=2, tag_latency=2, data_latency=2, response_latency=2, mshrs=4, tgts_per_mshr=20)
            cpu.dcache = Cache(size='32KiB', assoc=2, tag_latency=2, data_latency=2, response_latency=2, mshrs=16, tgts_per_mshr=20)
            cpu.l2cache = Cache(size='256KiB', assoc=8, tag_latency=10, data_latency=10, response_latency=10, mshrs=20, tgts_per_mshr=12)
            cpu.l2bus = L2XBar()

            cpu.connect_icache(cpu.icache.cpu_side)
            cpu.connect_dcache(cpu.dcache.cpu_side)
            cpu.icache.mem_side = cpu.l2bus.cpu_side_ports
            cpu.dcache.mem_side = cpu.l2bus.cpu_side_ports
            cpu.l2cache.cpu_side = cpu.l2bus.mem_side_ports
            cpu.l2cache.mem_side = board.l3bus.cpu_side_ports
            
            # FS 模式下，中断和页表 Walker 必须接在系统主总线 membus 上
            cpu.connect_walker_ports(self.membus.cpu_side_ports, self.membus.cpu_side_ports)
            cpu.connect_interrupt(self.membus.mem_side_ports, self.membus.cpu_side_ports)

# 下面保持不变，但在实例化 Board 时，gem5 会自动调用上面的 get_mem_side_port()

# --- 3. 组装 Board ---
requires(isa_required=ISA.X86, kvm_required=False)

cache_hierarchy = XptClassicHierarchy()
memory = DualChannelDDR4_2400(size="3GiB")
processor = SimpleSwitchableProcessor(
    starting_core_type=CPUTypes.ATOMIC, # FS 启动必须用 ATOMIC 绕过虚拟化限制
    switch_core_type=CPUTypes.TIMING, 
    isa=ISA.X86,
    num_cores=2,
)

board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# --- 4. 设置 PARSEC 工作负载 ---
command = (
    f"cd /home/gem5/parsec-benchmark; source env.sh; "
    f"parsecmgmt -a run -p {args.benchmark} -c gcc-hooks -i {args.size} -n 2; "
    f"m5 exit;"
)

# --- 5. 设置工作负载 ---
# 在这里定义内核启动参数，明确指定 root 为 /dev/sda1
board.set_kernel_disk_workload(
    kernel=KernelResource(local_path=local_vmlinux_path),
    disk_image=DiskImageResource(
        local_path=local_disk_path,
        root_partition="1" 
    ),
    readfile_contents=command,
    # 【关键修改】：显式指定 root 路径
    kernel_args=["lpj=7999923", "root=/dev/sda1", "console=ttyS0"]
)

# --- 5. 退出事件处理 ---
def handle_workbegin():
    print("Done booting Linux. Resetting stats and switching to TIMING cores...")
    m5.stats.reset()
    processor.switch() 
    yield False

def handle_workend():
    print("ROI finished. Dumping stats...")
    m5.stats.dump()
    yield True

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.WORKBEGIN: handle_workbegin(),
        ExitEvent.WORKEND: handle_workend(),
    },
)

# --- 6. 运行仿真 ---
print(f"Starting PARSEC {args.benchmark} with XPTGuard={args.defense}")
globalStart = time.time()
simulator.run()

print(f"\nFinal Simulated ROI Ticks: {simulator.get_roi_ticks()[0]}")
print(f"Total Wallclock Time: {(time.time() - globalStart)/60:.2f} min")