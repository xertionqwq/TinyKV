#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>

#include "net/eventloop.h"
#include "net/tcpConnection.h"
#include "net/acceptor.h"

#include "tiny_kv/db.h"

#include "protocol.h"

void test(uint16_t port, const std::string &filePath) {
    using namespace tiny_kv;
    DB db;
    db.Open(filePath);

    std::vector<std::shared_ptr<TcpConnection>> connections; // 保住 conn 不被析构

    EventLoop loop;
    auto looptr = &loop;
    InetAddress listenAddr(port);
    Acceptor ac(looptr, listenAddr);
    ac.setNewConnectionCallback([&](int connfd, const InetAddress&)
                                {
        auto conn = std::make_shared<TcpConnection>(looptr, connfd, port);
        connections.push_back(conn);  // 持有，保住命
        conn->setMessageCallback([&](const TcpConnection::TcpConnectionPtr& c, Buffer& buf) {
            // 1. 取出所有数据
            std::string raw = buf.retrieveAsString(buf.readableBytes());

            // 2. 按换行拆成多条命令，逐条处理
            std::string response;
            size_t pos = 0;
            while (pos < raw.size()) {
                size_t nl = raw.find('\n', pos);
                if (nl == std::string::npos) break;  // 不完整，跳过

                std::string line = raw.substr(pos, nl - pos);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                pos = nl + 1;

                if (line.empty()) continue;

                Command cmd = ParseCommand(line);
                std::string value;
                switch (cmd.type) {
                case Command::Type::PUT:
                    db.Put(cmd.key, cmd.value);
                    response += "OK\n";
                    break;
                case Command::Type::GET:
                    response += (db.Get(cmd.key, &value) == Status::OK)
                                    ? value + "\n" : "NOT_FOUND\n";
                    break;
                case Command::Type::DELETE:
                    db.Delete(cmd.key);
                    response += "OK\n";
                    break;
                default:
                    response += "ERROR\n";
                }
            }

            // 3. 积攒的响应一起发
            if (!response.empty())
                c->send(response);
        });
        conn->connectEstablished();
    });

    loop.loop();
}

int main(int argc, const char *argv[]) {
    if (argc != 3) {
		std::cerr << "Usage: ./server <port> <db_path>" << std::endl;
		return -1;
	}

    test(static_cast<uint16_t>(std::stoi(argv[1])), argv[2]);

    std::cout << "return done" << std::endl;
    return 0;
}
