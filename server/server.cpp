/*
	Tiny_KV 网络服务端
	阻塞式单连接 TCP 服务端，支持文本行协议 + RESP 协议
	累积缓冲区处理 TCP 粘包/半包
*/

#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

#include "protocol.h"

// === TCP 服务端 ==============================================================

class TcpServer {
public:
	int m_listenfd;
	int m_clientfd;
	std::string m_clientip;
	unsigned short m_port;

	TcpServer() : m_listenfd(-1), m_clientfd(-1) {}

	bool Init(unsigned short in_port) {
		m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
		if (m_listenfd == -1) return false;

		int opt = 1;
		setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

		m_port = in_port;

		struct sockaddr_in seraddr;
		memset(&seraddr, 0, sizeof(seraddr));
		seraddr.sin_family = AF_INET;
		seraddr.sin_port = htons(m_port);
		seraddr.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(m_listenfd, (sockaddr*)&seraddr, sizeof(seraddr)) == -1) {
			close(m_listenfd);
			m_listenfd = -1;
			return false;
		}

		if (listen(m_listenfd, 5) == -1) {
			close(m_listenfd);
			m_listenfd = -1;
			return false;
		}

		return true;
	}

	bool Accept() {
		struct sockaddr_in caddr;
		socklen_t addrlen = sizeof(caddr);

		m_clientfd = ::accept(m_listenfd, (sockaddr*)&caddr, &addrlen);
		if (m_clientfd == -1) return false;

		m_clientip = inet_ntoa(caddr.sin_addr);
		return true;
	}

	const std::string& ClientIP() const { return m_clientip; }

	bool Send(const std::string& buffer) {
		if (m_clientfd == -1) return false;
		if (::send(m_clientfd, buffer.data(), buffer.size(), 0) <= 0)
			return false;
		return true;
	}

	bool Recv(std::string& buffer, const size_t maxlen) {
		buffer.clear();
		buffer.resize(maxlen);

		int readn = ::recv(m_clientfd, &buffer[0], buffer.size(), 0);
		if (readn <= 0) {
			buffer.clear();
			return false;
		}

		buffer.resize(static_cast<size_t>(readn));
		return true;
	}

	bool CloseClient() {
		if (m_clientfd == -1) return false;
		::close(m_clientfd);
		m_clientfd = -1;
		return true;
	}

	bool CloseListen() {
		if (m_listenfd == -1) return false;
		::close(m_listenfd);
		m_listenfd = -1;
		return true;
	}

	~TcpServer() {
		CloseClient();
		CloseListen();
	}
};

// === 处理一条命令 ============================================================

static std::string HandleCommand(tiny_kv::DB& db, const Command& cmd, bool is_resp) {
	std::string value;
	tiny_kv::Status st;

	switch (cmd.type) {
	case Command::Type::GET:
		st = db.Get(cmd.key, &value);
		break;
	case Command::Type::PUT:
		st = db.Put(cmd.key, cmd.value);
		break;
	case Command::Type::DELETE:
		st = db.Delete(cmd.key);
		break;
	default:
		return is_resp ? "-ERR\r\n" : "ERROR\n";
	}

	return is_resp ? FormatRESPResponse(st, value) : FormatTextResponse(st, value);
}

// === main ===================================================================

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "Usage: ./server <port> <db_path>" << std::endl;
		return -1;
	}

	tiny_kv::DB db;
	tiny_kv::Status s = db.Open(argv[2]);
	if (s != tiny_kv::Status::OK) {
		std::cerr << "Failed to open DB" << std::endl;
		return -1;
	}
	std::cerr << "DB opened: " << argv[2] << std::endl;

	TcpServer server;
	if (!server.Init(static_cast<unsigned short>(atoi(argv[1])))) {
		std::perror("initserver");
		return -1;
	}
	std::cerr << "Listening on port " << argv[1] << std::endl;

	while (true) {
		if (!server.Accept()) {
			std::perror("accept");
			continue;
		}
		std::cerr << "Client connected: " << server.ClientIP() << std::endl;

		std::string buf;
		std::string tmp;
		while (server.Recv(tmp, 4096)) {
			buf += tmp;

			std::string responses;
			while (true) {
				bool is_resp = false;
				std::string cmd_str;

				// 尝试 RESP 协议（首字节 '*'）
				if (!buf.empty() && buf[0] == '*') {
					int len = TryConsumeRESP(buf);
					if (len > 0) {
						cmd_str = buf.substr(0, static_cast<size_t>(len));
						buf.erase(0, static_cast<size_t>(len));
						is_resp = true;
					} else if (len == 0) {
						break;  // 数据不完整
					}
					// len == -1: 格式错, 忽略这次
				}

				// 尝试文本协议（找 \n 分隔）
				if (cmd_str.empty() && !buf.empty()) {
					size_t nl = buf.find('\n');
					if (nl != std::string::npos) {
						cmd_str = buf.substr(0, nl);
						if (!cmd_str.empty() && cmd_str.back() == '\r')
							cmd_str.pop_back();
						buf.erase(0, nl + 1);
					} else {
						break;  // 还没收到完整行
					}
				}

				if (cmd_str.empty()) break;  // buf 空了, 等下次 recv

				std::cerr << "Request: " << cmd_str << std::endl;
				auto cmd = ParseCommand(cmd_str);
				responses += HandleCommand(db, cmd, is_resp);
			}

			if (!responses.empty())
				server.Send(responses);
		}

		std::cerr << "Client disconnected" << std::endl;
		server.CloseClient();
	}

	db.Close();
	return 0;
}
