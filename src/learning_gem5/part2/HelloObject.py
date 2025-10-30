# Copyright (c) 2017 Jason Lowe-Power
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.params import *
from m5.SimObject import SimObject


class HelloObject(SimObject):
    type = "HelloObject"
    cxx_header = "learning_gem5/part2/hello_object.hh"
    cxx_class = "gem5::HelloObject"

    time_to_wait = Param.Latency("Time before firing the event")  # 每一次调用事件 processEvent 的时间间隔
    number_of_fires = Param.Int(  # 调用事件 processEvent 的次数，然后就结束
        1, "Number of times to fire the event before goodbye"
    )

    goodbye_object = Param.GoodbyeObject("A goodbye object")


class GoodbyeObject(SimObject):  # 为硬件操作（如内存写入、网络发包）建立时间模型
    # GoodbyeObject 是真正负责退出仿真的对象。它在 fillBuffer 的最后调用了 exitSimLoop()，这会使 m5.simulate() 返回，从而结束整个 Python 脚本
    type = "GoodbyeObject"
    cxx_header = "learning_gem5/part2/goodbye_object.hh"
    cxx_class = "gem5::GoodbyeObject"

    buffer_size = Param.MemorySize(
        "1KiB", "Size of buffer to fill with goodbye"
    )
    write_bandwidth = Param.MemoryBandwidth(
        "100MiB/s", "Bandwidth to fill the buffer"
    )

# HelloObject 的事件（processEvent）按计划运行 5 次 。HelloObject::processEvent 不再调度自己（else 分支没有被执行） 。
# 取而代之，它执行了 if 语句块中的代码：goodbye->sayGoodbye(myName); 这是一个普通的 C++ 函数调用，它“激活”了 GoodbyeObject 。
# GoodbyeObject 的 sayGoodbye 函数启动了它自己的事件循环（即 fillBuffer 和 schedule(event, ...)）