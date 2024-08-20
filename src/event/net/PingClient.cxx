// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PingClient.hxx"
#include "net/in_cksum.hxx"
#include "net/IPv4Address.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/SendMessage.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Iovec.hxx"
#include "util/SpanCast.hxx"

#include <cassert>
#include <stdexcept>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <unistd.h>

PingClient::PingClient(EventLoop &event_loop,
		       PingClientHandler &_handler) noexcept
	:event(event_loop, BIND_THIS_METHOD(EventCallback)),
	 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
	 handler(_handler)
{
}

inline void
PingClient::ScheduleRead() noexcept
{
	event.ScheduleRead();
	timeout_event.Schedule(std::chrono::seconds(10));
}

static bool
parse_reply(const struct msghdr &msg, size_t cc, uint16_t ident) noexcept
{
	const std::byte *buf = reinterpret_cast<const std::byte *>(msg.msg_iov->iov_base);
	const struct icmphdr &icp = *(const struct icmphdr *)(const void *)buf;
	if (cc < sizeof(icp))
		return false;

	return icp.type == ICMP_ECHOREPLY && icp.un.echo.id == ident &&
	       in_cksum({buf, cc}, 0) == 0;
}

inline void
PingClient::Read() noexcept
{
	char buffer[1024];
	auto iov = MakeIovecT(buffer);

	struct msghdr msg;
	memset(&msg, 0, sizeof(msg));

	char addrbuf[128];
	msg.msg_name = addrbuf;
	msg.msg_namelen = sizeof(addrbuf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	char ans_data[4096];
	msg.msg_control = ans_data;
	msg.msg_controllen = sizeof(ans_data);

	int cc = event.GetSocket().Receive(msg, MSG_DONTWAIT);
	if (cc >= 0) {
		if (parse_reply(msg, cc, ident)) {
			event.Close();
			timeout_event.Cancel();
			handler.PingResponse();
		}
	} else if (const auto e = GetSocketError(); !IsSocketErrorReceiveWouldBlock(e)) {
		event.Close();
		timeout_event.Cancel();
		handler.PingError(std::make_exception_ptr(MakeSocketError(e, "Failed to receive ping reply")));
	}
}


/*
 * libevent callback
 *
 */

inline void
PingClient::EventCallback(unsigned) noexcept
{
	assert(event.IsDefined());

	Read();
}

inline void
PingClient::OnTimeout() noexcept
{
	assert(event.IsDefined());

	event.Close();
	handler.PingTimeout();
}

/*
 * constructor
 *
 */

bool
PingClient::IsAvailable() noexcept
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

static UniqueSocketDescriptor
CreateIcmp()
{
	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(AF_INET, SOCK_DGRAM, IPPROTO_ICMP))
		throw MakeSocketError("Failed to create ICMP socket");

	return fd;
}

static uint16_t
MakeIdent(SocketDescriptor fd)
{
	if (!fd.Bind(IPv4Address(0)))
		throw MakeSocketError("Failed to bind ICMP socket");

	const auto address = fd.GetLocalAddress();
	if (!address.IsDefined())
		throw MakeSocketError("Failed to inspect ICMP socket");

	switch (address.GetFamily()) {
	case AF_INET:
		return IPv4Address::Cast(address).GetPortBE();

	default:
		throw std::runtime_error{"Unsupported address family"};
	}
}

static void
SendPing(SocketDescriptor fd, SocketAddress address, uint16_t ident)
{
	static constexpr std::byte payload[8]{};

	struct icmphdr header{
		.type = ICMP_ECHO,
		.un = {
			.echo = {
				.id = ident,
				.sequence = htons(1),
			},
		},
	};

	header.checksum = in_cksum(std::span{payload}, in_cksum(ReferenceAsBytes(header), 0));

	const std::array iov{MakeIovecT(header), MakeIovec(payload)};

	SendMessage(fd,
		    MessageHeader(iov)
		    .SetAddress(address), 0);
}

void
PingClient::Start(SocketAddress address) noexcept
{
	try {
		event.Open(CreateIcmp().Release());
		ident = MakeIdent(event.GetSocket());
		SendPing(event.GetSocket(), address, ident);
	} catch (...) {
		handler.PingError(std::current_exception());
		return;
	}

	ScheduleRead();
}
