/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "learning_gem5/part2/goodbye_object.hh"

#include "base/trace.hh"
#include "debug/HelloExample.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

GoodbyeObject::GoodbyeObject(const GoodbyeObjectParams &params) :
    SimObject(params), event([this]{ processEvent(); }, name() + ".event"),
    bandwidth(params.write_bandwidth), bufferSize(params.buffer_size),
    buffer(nullptr), bufferUsed(0)
{
    buffer = new char[bufferSize]();
    DPRINTF(HelloExample, "Created the goodbye object\n");
}

GoodbyeObject::~GoodbyeObject()
{
    delete[] buffer;
}

void
GoodbyeObject::processEvent()
{
    DPRINTF(HelloExample, "Processing the event!\n");

    // Actually do the "work" of the event
    fillBuffer();
}

void
GoodbyeObject::sayGoodbye(std::string other_name)  // 1. sayGoodbye 是触发器。它设置了要写入的 message，并立即调用 fillBuffer() 开始第一次写入
{
    DPRINTF(HelloExample, "Saying goodbye to %s\n", other_name);

    message = "Goodbye " + other_name + "!! ";

    // Kick off the the first buffer fill. If it can't fill the whole buffer
    // because of a limited bandwidth, then this function will schedule another
    // event to finish the fill
    fillBuffer();
}

void
GoodbyeObject::fillBuffer()
{
    // There better be a message
    assert(message.length() > 0);

    // Copy from the message to the buffer per byte.
    int bytes_copied = 0;
    for (auto it = message.begin();
         it < message.end() && bufferUsed < bufferSize - 1;
         it++, bufferUsed++, bytes_copied++) {
        // Copy the character into the buffer
        buffer[bufferUsed] = *it;
    }

    if (bufferUsed < bufferSize - 1) {
        // Wait for the next copy for as long as it would have taken
        // 未满：说明这个 message 已经完整复制进去了，但缓冲区还有空间，所以我们要 "再写一遍"，会调度下一次 event (它会再次调用 fillBuffer)
        DPRINTF(HelloExample, "Scheduling another fillBuffer in %d ticks\n",
                bandwidth * bytes_copied);
        schedule(event, curTick() + bandwidth * bytes_copied);
    } else {
        DPRINTF(HelloExample, "Goodbye done copying!\n");
        // Be sure to take into account the time for the last bytes
        // 已满：任务完成！调度 "退出仿真" 事件
        exitSimLoop(buffer, 0, curTick() + bandwidth * bytes_copied);
    }
}

} // namespace gem5
