#include <sys/socket.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <iostream>

#include "net/eventloop.h"
#include "net/channel.h"
#include "net/inetAddress.h"
#include "net/acceptor.h"

using namespace tiny_kv;

void test_channel_read() {
    int fds[2];
    int ret = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    assert(ret == 0);

    EventLoop loop;
    Channel ch(&loop, fds[0]);

    bool called = false;
    ch.setReadCallback([&]() {
        char buf[16];
        ::recv(fds[0], buf, sizeof(buf), 0);
        called = true;
        loop.quit();
    });
    ch.enableReading();

    ::write(fds[1], "x", 1);
    loop.loop();  // 事件到达 → 回调 → quit → loop 返回

    ::close(fds[0]);
    ::close(fds[1]);
    assert(called && "read callback should have fired");
}

void test_channel_write() {
    int fds[2];
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);

    EventLoop loop;
    Channel ch(&loop, fds[0]);
    ch.enableWriting();

    bool called = false;
    ch.setWriteCallback([&]() {
        called = true;
        ch.disableWriting();
        loop.quit();
    });
    ch.enableWriting();

    loop.loop();

    ::close(fds[0]);
    ::close(fds[1]);
    assert(called && "write callback should have fired");
}

int main() {
    test_channel_read();
    std::cout << "[1/2] test_channel_read passed" << std::endl;

    test_channel_write();
    std::cout << "[2/2] test_channel_write passed" << std::endl;

    std::cout << "test_net: all passed" << std::endl;
    return 0;
}
