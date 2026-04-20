import m5
from m5.objects import *
import os

import argparse

parser = argparse.ArgumentParser()
# 修改这里：使用 type=int 或者字符串判断，避开 store_true 的限制
parser.add_argument("--defense", type=int, default=0, help="1:Enable, 0:Disable")
parser.add_argument("--mode", type=str, default="vGLO", choices=["vID", "vGLO"])
parser.add_argument("--threshold", type=str, default="", help="Oracle threshold")
args = parser.parse_args()

system = System()
system.clk_domain = SrcClockDomain(clock='3GHz', voltage_domain=VoltageDomain())
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('2GB')]

# # 1. 核心与总线配置 
# system.cpu = [X86TimingSimpleCPU(cpu_id=i) for i in range(2)]
# system.membus = SystemXBar()
# system.l3bus = L2XBar()

# 1. 核心与总线配置 
# 修改点：将 range(1) 改回 range(2)，释放出 Core 1
system.cpu = [X86TimingSimpleCPU(cpu_id=i) for i in range(2)] 
system.membus = SystemXBar(clk_domain=system.clk_domain)
system.l3bus = L2XBar(clk_domain=system.clk_domain)

# 2. 共享 L3 缓存 (LLC) 与 XPT 预取器 
system.l3cache = Cache(size='2MB', assoc=16, 
                       tag_latency=60, data_latency=20, response_latency=20,
                       sequential_access=True,
                       mshrs=12, tgts_per_mshr=12)

# system.l3cache = Cache(size='128kB', assoc=2, 
#                        tag_latency=20, data_latency=20, response_latency=20,
#                        mshrs=8, tgts_per_mshr=12) # 减少 MSHR 也会增加竞争

system.l3cache.prefetcher = XptPrefetcher(
    num_entries = 256,         # XPT 容量 
    activation_threshold = 32  # 激活阈值 
    # 理由：让预取器在单次 Miss 后立即记录，极大加速实验成果的呈现
    # activation_threshold = 1,
)

# # system.l3cache.prefetcher.enable_defense = False  # 攻击 baseline
# system.l3cache.prefetcher.enable_defense = True  # 开启防御
# # system.l3cache.prefetcher.is_vGLO = False  # vID 防御
# system.l3cache.prefetcher.is_vGLO = True  # vGLO 防御

# ... 系统配置 ...
system.l3cache.prefetcher.enable_defense = bool(args.defense)
# 根据参数决定是 vID 还是 vGLO
if args.defense:
    system.l3cache.prefetcher.is_vGLO = (args.mode == "vGLO")
    print(f"[*] XPTGuard Active: Mode={args.mode}")

# 3. CPU 私有缓存配置 [cite: 3, 7]
for i, cpu in enumerate(system.cpu):
    # 显式指定所有延迟参数和 mshrs
    cpu.icache = Cache(size='32kB', assoc=2, 
                       tag_latency=2, data_latency=2, response_latency=2,
                       mshrs=4, tgts_per_mshr=20)
    
    cpu.dcache = Cache(size='32kB', assoc=2, 
                       tag_latency=2, data_latency=2, response_latency=2, # 修复点：添加延迟参数
                       mshrs=16, tgts_per_mshr=20)
    
    # 模拟真实架构，添加 L2 缓存 
    cpu.l2cache = Cache(size='256kB', assoc=8, 
                       tag_latency=10,        # 标签查找延迟
                       data_latency=10,       # 数据读取延迟
                       response_latency=10,   # 修复点：添加响应延迟
                       mshrs=20, 
                       tgts_per_mshr=12)
    cpu.l2bus = L2XBar(clk_domain=system.clk_domain)

    # 关键连接 CPU -> L1
    cpu.icache_port = cpu.icache.cpu_side
    cpu.dcache_port = cpu.dcache.cpu_side
    
    # 连接 L1 -> L2
    cpu.icache.mem_side = cpu.l2bus.cpu_side_ports
    cpu.dcache.mem_side = cpu.l2bus.cpu_side_ports
    
    # 关键连接：L2 总线 -> L2 缓存
    cpu.l2cache.cpu_side = cpu.l2bus.mem_side_ports
    
    # 连接 L2 缓存 -> L3 总线 
    cpu.l2cache.mem_side = system.l3bus.cpu_side_ports
    
    # 中断控制器连接 
    cpu.createInterruptController()
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# 4. 全局总线连接
system.l3cache.cpu_side = system.l3bus.mem_side_ports
system.l3cache.mem_side = system.membus.cpu_side_ports

# 5. 内存与工作负载 
system.mem_ctrl = MemCtrl(dram=DDR4_2400_8x8(range=system.mem_ranges[0], tCL='25ns', tRP='25ns', tRCD='25ns'))
system.mem_ctrl.port = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

# binary_a = './attacker'
# binary_v = './victim'

# system.workload = SEWorkload.init_compatible(binary_a)

# # 显式指定 PID 以彻底解决冲突问题 
# # 在 m5.instantiate() 之前修改
# # 显式给攻击者更多的指令额度或先运行
# system.cpu[0].max_insts_any_thread = 100000000 # 确保攻击者能跑完
# system.cpu[0].workload = Process(executable=binary_a, cmd=[binary_a], pid=100)
# system.cpu[1].workload = Process(executable=binary_v, cmd=[binary_v], pid=200)

# for cpu in system.cpu:
#     cpu.createThreads()

# 修改点：目标程序改为你刚刚编译的多进程 test
binary = './two_core_test'
system.workload = SEWorkload.init_compatible(binary)

# 修改点：只给核心 0 分配 workload
# 如果有传入阈值，就加到命令行参数里
if args.threshold and args.threshold.strip():
    cmd_list = [binary, args.threshold]
else:
    cmd_list = [binary]

process = Process(executable=binary, cmd=cmd_list, pid=100)

# process = Process(executable=binary, cmd=[binary], pid=100)
# 关键修改：将这一个 process 映射到所有的 CPU 上，并为它们都创建线程
for cpu in system.cpu:
    cpu.workload = process
    cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
print("Beginning simulation!")
m5.simulate()