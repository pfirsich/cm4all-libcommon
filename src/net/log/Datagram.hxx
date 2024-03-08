// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Protocol.hxx"
#include "Chrono.hxx"

#include <chrono>
#include <cstdint>
#include <string_view>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;

namespace Net::Log {

struct Datagram {
	TimePoint timestamp = TimePoint();

	const char *remote_host = nullptr, *host = nullptr, *site = nullptr;

	const char *forwarded_to = nullptr;

	const char *http_uri = nullptr, *http_referer = nullptr;
	const char *user_agent = nullptr;

	std::string_view message{};

	std::string_view json{};

	uint_least64_t length;

	uint_least64_t traffic_received, traffic_sent;

	Duration duration;

	HttpMethod http_method = {};

	HttpStatus http_status = {};

	Type type = Type::UNSPECIFIED;

	bool valid_length = false, valid_traffic = false;
	bool valid_duration = false;

	constexpr Datagram() = default;

	explicit constexpr Datagram(Type _type) noexcept
		:type(_type) {}

	constexpr Datagram(TimePoint _timestamp,
			   HttpMethod _method, const char *_uri,
			   const char *_remote_host,
			   const char *_host, const char *_site,
			   const char *_referer, const char *_user_agent,
			   HttpStatus _status, int64_t _length,
			   uint_least64_t _traffic_received,
			   uint_least64_t _traffic_sent,
			   Duration _duration) noexcept
		:timestamp(_timestamp),
		 remote_host(_remote_host), host(_host), site(_site),
		 http_uri(_uri), http_referer(_referer), user_agent(_user_agent),
		 length(_length),
		 traffic_received(_traffic_received), traffic_sent(_traffic_sent),
		 duration(_duration),
		 http_method(_method),
		 http_status(_status),
		 type(Type::HTTP_ACCESS),
		 valid_length(_length >= 0), valid_traffic(true),
		 valid_duration(true) {}

	constexpr Datagram(Type _type, std::string_view _message) noexcept
		:message(_message), type(_type) {}

	constexpr bool HasTimestamp() const noexcept {
		return timestamp != TimePoint();
	}

	constexpr void SetTimestamp(TimePoint t) noexcept {
		timestamp = t;
	}

	constexpr void SetTimestamp(std::chrono::system_clock::time_point t) noexcept {
		SetTimestamp(FromSystem(t));
	}

	constexpr bool HasHttpMethod() const noexcept {
		return http_method != HttpMethod{};
	}

	constexpr bool HasHttpStatus() const noexcept {
		return http_status != HttpStatus{};
	}

	constexpr bool GuessIsHttpAccess() const noexcept {
		return http_uri != nullptr && HasHttpMethod() &&
			/* the following matches cancelled HTTP
			   requests (that have no HTTP status), but
			   rejects HTTP error messages (via
			   #valid_traffic; HTTP error messages have no
			   traffic) */
			(HasHttpStatus() || valid_traffic);
	}

	constexpr bool IsHttpAccess() const noexcept {
		return type == Type::HTTP_ACCESS ||
			(type == Type::UNSPECIFIED && GuessIsHttpAccess());
	}
};

} // namespace Net::Log
