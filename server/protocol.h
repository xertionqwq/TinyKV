/*
	协议层 — 文本行协议 + RESP 解析 + 响应格式化
	header-only，Command 结构体是 DB 调用的唯一中间层
*/

#ifndef TINY_KV_SERVER_PROTOCOL_H_
#define TINY_KV_SERVER_PROTOCOL_H_

#include <string>
#include <vector>

#include "tiny_kv/db.h"

// === 命令结构体（协议无关）===================================================

struct Command {
	enum class Type : uint8_t { GET, PUT, DELETE, UNKNOWN };
	Type type = Type::UNKNOWN;
	std::string key;
	std::string value;
};

// === 内部辅助 ===============================================================

static std::vector<std::string> Split(const std::string& s, char delim) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= s.size()) {
		size_t end = s.find(delim, start);
		if (end == std::string::npos) {
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, end - start));
		start = end + 1;
	}
	return parts;
}

// 从 vector<string> 构造 Command，和 ParseText 共享
static Command PartsToCommand(const std::vector<std::string>& parts) {
	if (parts.empty()) return {};

	if (parts[0] == "GET" && parts.size() >= 2)
		return {Command::Type::GET, parts[1], ""};
	if (parts[0] == "SET" && parts.size() >= 3)
		return {Command::Type::PUT, parts[1], parts[2]};
	if (parts[0] == "PUT" && parts.size() >= 3)
		return {Command::Type::PUT, parts[1], parts[2]};
	if (parts[0] == "DEL" && parts.size() >= 2)
		return {Command::Type::DELETE, parts[1], ""};
	if (parts[0] == "DELETE" && parts.size() >= 2)
		return {Command::Type::DELETE, parts[1], ""};

	return {};
}

// === RESP 内部辅助 ==========================================================

// 读 \r\n 结尾的整数。返回 -2 = 数据不完整, -1 = 格式错
static int ReadRESPLen(const std::string& buf, size_t& pos) {
	auto cr = buf.find('\r', pos);
	if (cr == std::string::npos)   return -2;
	if (cr + 1 >= buf.size())       return -2;
	if (buf[cr + 1] != '\n')        return -1;

	int val = 0;
	for (size_t i = pos; i < cr; ++i) {
		if (buf[i] < '0' || buf[i] > '9') return -1;
		val = val * 10 + (buf[i] - '0');
	}
	pos = cr + 2;
	return val;
}

// 判断缓冲区里是否有一条完整的 RESP 命令
// 返回 >0 = 命令字节数, 0 = 数据不完整, -1 = 格式错
static int TryConsumeRESP(const std::string& buf) {
	if (buf.empty() || buf[0] != '*') return -1;

	size_t pos = 1;
	int n = ReadRESPLen(buf, pos);
	if (n < 0) return n;

	for (int i = 0; i < n; ++i) {
		if (pos >= buf.size())      return 0;
		if (buf[pos] != '$')        return -1;
		pos++;

		int len = ReadRESPLen(buf, pos);
		if (len < 0)                return len;
		if (pos + len + 2 > buf.size()) return 0;
		pos += len + 2;
	}
	return static_cast<int>(pos);
}

// === 文本协议解析 ===========================================================

inline Command ParseText(const std::string& line) {
	auto parts = Split(line, ' ');
	return PartsToCommand(parts);
}

// === RESP 协议解析 ==========================================================

// raw 必须是一条完整的 RESP 命令（已由 TryConsumeRESP 验证）
inline Command ParseRESP(const std::string& raw) {
	size_t pos = 1;
	int n = ReadRESPLen(raw, pos);  // 必定成功

	std::vector<std::string> parts;
	for (int i = 0; i < n; ++i) {
		pos++;  // 跳过 '$'
		int len = ReadRESPLen(raw, pos);
		parts.push_back(raw.substr(pos, static_cast<size_t>(len)));
		pos += static_cast<size_t>(len) + 2;
	}
	return PartsToCommand(parts);
}

// === 统一入口（首字节分发）==================================================

inline Command ParseCommand(const std::string& msg, bool* is_resp = nullptr) {
	if (msg.empty()) return {};
	bool resp = (msg[0] == '*');
	if (is_resp) *is_resp = resp;
	return resp ? ParseRESP(msg) : ParseText(msg);
}

// === 响应格式化 =============================================================

inline std::string FormatTextResponse(tiny_kv::Status st, const std::string& value) {
	if (st == tiny_kv::Status::OK) {
		if (value.empty()) return "OK\n";
		return value + "\n";
	}
	if (st == tiny_kv::Status::NotFound) return "NOT_FOUND\n";
	return "ERROR\n";
}

inline std::string FormatRESPResponse(tiny_kv::Status st, const std::string& value) {
	if (st == tiny_kv::Status::OK) {
		if (value.empty()) return "+OK\r\n";
		return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
	}
	if (st == tiny_kv::Status::NotFound) return "$-1\r\n";
	return "-ERR\r\n";
}

#endif
