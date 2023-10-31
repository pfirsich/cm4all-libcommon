// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ResolvedClient.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "event/SocketEvent.hxx"
#include "system/Error.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/IPv4Address.hxx"
#include "net/IPv6Address.hxx"
#include "util/IntrusiveList.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"

#define JSON_NO_IO
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <cassert>

namespace Systemd {

using std::string_view_literals::operator""sv;

static json
JsonResolveHostname(std::string_view hostname)
{
	return {
		{"method"sv, "io.systemd.Resolve.ResolveHostname"sv},
		{"parameters"sv, {
				{"name"sv, hostname},
				{"flags"sv, 0},
			},
		},
	};
}

static std::string
SerializeResolveHostname(std::string_view hostname)
{
	std::string s = JsonResolveHostname(hostname).dump();
	s.push_back('\0');
	return s;
}

class ResolveHostnameRequest final : Cancellable {
	SocketEvent socket;
	const uint_least16_t port;
	ResolveHostnameHandler &handler;

public:
	ResolveHostnameRequest(EventLoop &event_loop, UniqueSocketDescriptor &&_socket,
			       uint_least16_t _port, ResolveHostnameHandler &_handler) noexcept
		:socket(event_loop, BIND_THIS_METHOD(OnSocketReady), _socket.Release()),
		 port(_port), handler(_handler)
	{
	}

	~ResolveHostnameRequest() noexcept {
		socket.Close();
	}

	void Start(std::string_view hostname,
		   CancellablePointer &cancel_ptr) {
		auto nbytes = socket.GetSocket().Send(AsBytes(SerializeResolveHostname(hostname)));
		if (nbytes < 0)
			throw MakeErrno("Failed to send");

		socket.ScheduleRead();
		cancel_ptr = *this;
	}

private:
	void OnResponse(std::string_view s);
	void OnSocketReady(unsigned events) noexcept;

	/* virtual methods from Cancellable */
	void Cancel() noexcept override {
		delete this;
	}
};

static IPv4Address
ToIPv4Address(const json &j, uint_least16_t port)
{
	if (j.size() != 4)
		throw SocketProtocolError{"Malformed IPv4 address"};

	return {
		j.at(0).get<uint8_t>(),
		j.at(1).get<uint8_t>(),
		j.at(2).get<uint8_t>(),
		j.at(3).get<uint8_t>(),
		port,
	};
}

static IPv6Address
ToIPv6Address(const json &j, uint_least16_t port,
	      uint_least32_t ifindex)
{
	if (j.size() != 16)
		throw SocketProtocolError{"Malformed IPv6 address"};

	return {
		static_cast<uint_least16_t>((j.at(0).get<uint_least16_t>() << 8) |
					    j.at(1).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(2).get<uint_least16_t>() << 8) |
					    j.at(3).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(4).get<uint_least16_t>() << 8) |
					    j.at(5).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(6).get<uint_least16_t>() << 8) |
					    j.at(7).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(8).get<uint_least16_t>() << 8) |
					    j.at(9).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(10).get<uint_least16_t>() << 8) |
					    j.at(11).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(12).get<uint_least16_t>() << 8) |
					    j.at(13).get<uint_least16_t>()),
		static_cast<uint_least16_t>((j.at(14).get<uint_least16_t>() << 8) |
					    j.at(15).get<uint_least16_t>()),
		port,
		ifindex,
	};
}

inline void
ResolveHostnameRequest::OnResponse(std::string_view s)
{
	assert(!s.empty());

	if (s.back() != '\0')
		throw SocketProtocolError("Malformed response");

	s.remove_suffix(1);

	const auto j = json::parse(s);

	if (const auto error = j.find("error"sv); error != j.end())
		throw FmtRuntimeError("systemd-resolved error: {}",
				      error->dump());

	auto &a = j.at("parameters"sv).at("addresses"sv).at(0);

	const auto &address = a.at("address"sv);

	uint_least32_t ifindex = 0;
	if (const auto i = a.find("ifindex"sv); i != a.end())
		ifindex = i->get<uint_least32_t>();

	switch (a.at("family"sv).get<int>()) {
	case AF_INET:
		handler.OnResolveHostname(ToIPv4Address(address, port));
		break;

	case AF_INET6:
		handler.OnResolveHostname(ToIPv6Address(address, port, ifindex));
		break;

	default:
		throw SocketProtocolError{"Unsupported address family"};
	}
}

void
ResolveHostnameRequest::OnSocketReady(unsigned events) noexcept
try {
	if (events & (socket.ERROR|socket.HANGUP))
		throw SocketClosedPrematurelyError{};

	char buffer[4096];
	auto nbytes = socket.GetSocket().Receive(std::as_writable_bytes(std::span{buffer}));
	if (nbytes < 0)
		throw MakeErrno("Failed to receive");

	socket.Close();

	if (nbytes == 0)
		throw SocketClosedPrematurelyError{};

	OnResponse(ToStringView(std::span{buffer}.first(nbytes)));
	delete this;
} catch (...) {
	auto &_handler = handler;
	delete this;
	_handler.OnResolveHostnameError(std::current_exception());
}

static UniqueSocketDescriptor
ConnectResolved()
{
	AllocatedSocketAddress address;
	address.SetLocal("/run/systemd/resolve/io.systemd.Resolve");
	return CreateConnectSocket(address, SOCK_STREAM);
}

void
ResolveHostname(EventLoop &event_loop,
		std::string_view hostname, unsigned port,
		ResolveHostnameHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
try {
	auto *request = new ResolveHostnameRequest(event_loop, ConnectResolved(),
						   port, handler);

	try {
		request->Start(hostname, cancel_ptr);
	} catch (...) {
		delete request;
		throw;
	}
} catch (...) {
	handler.OnResolveHostnameError(std::current_exception());
}

} // namespace Systemd