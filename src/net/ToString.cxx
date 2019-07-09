/*
 * Copyright 2007-2019 Content Management AG
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

#include "ToString.hxx"
#include "SocketAddress.hxx"
#include "IPv4Address.hxx"

#include <algorithm>

#include <assert.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <stdint.h>

static bool
LocalToString(char *buffer, size_t buffer_size,
	      const struct sockaddr_un *sun, size_t length) noexcept
{
	const size_t prefix = (size_t)((struct sockaddr_un *)nullptr)->sun_path;
	assert(length >= prefix);
	length -= prefix;
	if (length >= buffer_size)
		length = buffer_size - 1;

	memcpy(buffer, sun->sun_path, length);
	char *end = buffer + length;

	if (end > buffer && buffer[0] != '\0' && end[-1] == '\0')
		/* don't convert the null terminator of a non-abstract socket
		   to a '@' */
		--end;

	/* replace all null bytes with '@'; this also handles abstract
	   addresses */
	std::replace(buffer, end, '\0', '@');
	*end = 0;

	return true;
}

bool
ToString(char *buffer, size_t buffer_size,
	 SocketAddress address) noexcept
{
	if (address.IsNull())
		return false;

	if (address.GetFamily() == AF_LOCAL)
		return LocalToString(buffer, buffer_size,
				     (const struct sockaddr_un *)address.GetAddress(),
				     address.GetSize());

	IPv4Address ipv4_buffer;
	if (address.IsV4Mapped())
		address = ipv4_buffer = address.UnmapV4();

	char serv[16];
	int ret = getnameinfo(address.GetAddress(), address.GetSize(),
			      buffer, buffer_size,
			      serv, sizeof(serv),
			      NI_NUMERICHOST | NI_NUMERICSERV);
	if (ret != 0)
		return false;

	if (serv[0] != 0) {
		if (address.GetFamily() == AF_INET6) {
			/* enclose IPv6 address in square brackets */

			size_t length = strlen(buffer);
			if (length + 4 >= buffer_size)
				/* no more room */
				return false;

			memmove(buffer + 1, buffer, length);
			buffer[0] = '[';
			buffer[++length] = ']';
			buffer[++length] = 0;
		}

		if (strlen(buffer) + 1 + strlen(serv) >= buffer_size)
			/* no more room */
			return false;

		strcat(buffer, ":");
		strcat(buffer, serv);
	}

	return true;
}

const char *
ToString(char *buffer, size_t buffer_size, SocketAddress address,
	 const char *fallback) noexcept
{
	return ToString(buffer, buffer_size, address)
		? buffer
		: fallback;
}

bool
HostToString(char *buffer, size_t buffer_size,
	     SocketAddress address) noexcept
{
	if (address.IsNull())
		return false;

	if (address.GetFamily() == AF_LOCAL)
		return LocalToString(buffer, buffer_size,
				     (const struct sockaddr_un *)address.GetAddress(),
				     address.GetSize());

	IPv4Address ipv4_buffer;
	if (address.IsV4Mapped())
		address = ipv4_buffer = address.UnmapV4();

	return getnameinfo(address.GetAddress(), address.GetSize(),
			   buffer, buffer_size,
			   nullptr, 0,
			   NI_NUMERICHOST | NI_NUMERICSERV) == 0;
}
