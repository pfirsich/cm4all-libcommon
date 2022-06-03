/*
 * Copyright 2007-2022 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "QmqpClient.hxx"

#include <string> // TODO: migrate to std::string_view

using std::string_view_literals::operator""sv;

void
QmqpClient::AppendNetstring(std::string_view value) noexcept
{
	netstring_headers.emplace_front();
	auto &g = netstring_headers.front();
	request.emplace_back(std::as_bytes(std::span{g(value.size())}));
	request.emplace_back(std::as_bytes(std::span{value}));
	request.emplace_back(std::as_bytes(std::span{","sv}));
}

void
QmqpClient::Commit(FileDescriptor out_fd, FileDescriptor in_fd) noexcept
{
	assert(!netstring_headers.empty());
	assert(!request.empty());

	client.Request(out_fd, in_fd, std::move(request));
}

void
QmqpClient::OnNetstringResponse(AllocatedArray<std::byte> &&_payload) noexcept
try {
	std::string_view payload{(const char *)_payload.data(), _payload.size()};

	if (!payload.empty()) {
		switch (payload[0]) {
		case 'K':
			// success
			handler.OnQmqpClientSuccess(payload.substr(1));
			return;

		case 'Z':
			// temporary failure
			throw QmqpClientTemporaryFailure(std::string{payload.substr(1)});

		case 'D':
			// permanent failure
			throw QmqpClientPermanentFailure(std::string{payload.substr(1)});
		}
	}

	throw QmqpClientError("Malformed QMQP response");
} catch (...) {
	handler.OnQmqpClientError(std::current_exception());
}
