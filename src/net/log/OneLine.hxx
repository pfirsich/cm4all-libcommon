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

#pragma once

#include <cstddef>

class FileDescriptor;

namespace Net::Log {

struct Datagram;

struct OneLineOptions {
	/**
	 * Show the time stamp in ISO8601 format?
	 */
	bool iso8601 = false;

	/**
	 * Show the site name?
	 */
	bool show_site = false;

	/**
	 * Show the HTTP "Host" header?
	 */
	bool show_host = false;

	/**
	 * anonymize IP addresses by zeroing a portion at the end?
	 */
	bool anonymize = false;

	/**
	 * Show the address of the server this request was forwarded
	 * to?
	 */
	bool show_forwarded_to = false;

	/**
	 * Show the HTTP "Referer" header?
	 */
	bool show_http_referer = true;

	/**
	 * Show the HTTP "User-Agent" header?
	 */
	bool show_user_agent = true;
};

/**
 * Convert the given datagram to a text line (without a trailing
 * newline character and without a null terminator).
 *
 * @return a pointer to the end of the line
 */
char *
FormatOneLine(char *buffer, std::size_t buffer_size,
	      const Datagram &d, OneLineOptions options) noexcept;

/**
 * Print the #Datagram in one line, similar to Apache's "combined" log
 * format.
 *
 * @return true on success, false on error (errno set)
 */
bool
LogOneLine(FileDescriptor fd, const Datagram &d,
	   OneLineOptions options) noexcept;

} // namespace Net::Log
