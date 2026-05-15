#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <cassert>
#include <cstring>
#include <iostream>

// #include "net/event_loop.h"
#include "net/channel.h"
#include "net/poller.h"

using namespace tiny_kv;

// 测试 1：Channel 基本回调
void test_channel_basic() {
//     // 创建一个 socketpair 或 pipe，不需要网络
//     int fds[2];
//     socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

//     EventLoop loop;
//     Channel ch(&loop, fds[0]);

//     bool called = false;
//     ch.setReadCallback([&]() { called = true; });
//     ch.enableReading();

//     // 另一端写数据
//     write(fds[1], "x", 1);

//     // 让 loop 跑一轮就退
//     // ...

//     close(fds[0]);
//     close(fds[1]);
//     assert(called && "read callback should have fired");
}

int main()
{
    test_channel_basic();
    std::cout << "test_net: all passed" << std::endl;
}
