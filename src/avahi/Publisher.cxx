/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Publisher.hxx"
#include "Client.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv6Address.hxx"
#include "net/Interface.hxx"

#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/alternative.h>

#include <cassert>

#include <stdio.h>
#include <unistd.h>
#include <net/if.h>

namespace Avahi {

/**
 * Append the process id to the given prefix string.  This is used as
 * a workaround for an avahi-daemon bug/problem: when a service gets
 * restarted, and then binds to a new port number (e.g. beng-proxy
 * with automatic port assignment), we don't get notified, and so we
 * never query the new port.  By appending the process id to the
 * client name, we ensure that the exiting old process broadcasts
 * AVAHI_BROWSER_REMOVE, and hte new process broadcasts
 * AVAHI_BROWSER_NEW.
 */
static std::string
MakePidName(const char *prefix)
{
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s[%u]", prefix, (unsigned)getpid());
	return buffer;
}

Publisher::Publisher(Client &_client, const char *_name) noexcept
	:logger("avahi"), name(MakePidName(_name)),
	 client(_client)
{
	client.AddListener(*this);
}

Publisher::~Publisher() noexcept
{
	client.RemoveListener(*this);

	if (group != nullptr) {
		avahi_entry_group_free(group);
		group = nullptr;
	}
}

void
Publisher::AddService(AvahiIfIndex interface, AvahiProtocol protocol,
		      const char *type, uint16_t port) noexcept
{
	/* cannot register any more services after initial connect */
	assert(group == nullptr);

	services.emplace_front(interface, protocol, type, port);

	client.Activate();
}

void
Publisher::AddService(const char *type, const char *interface,
		      SocketAddress address, bool v6only) noexcept
{
	unsigned port = address.GetPort();
	if (port == 0)
		return;

	unsigned i = 0;
	if (interface != nullptr)
		i = if_nametoindex(interface);

	if (i == 0)
		i = FindNetworkInterface(address);

	AvahiIfIndex ii = i > 0
		? AvahiIfIndex(i)
		: AVAHI_IF_UNSPEC;

	AvahiProtocol protocol = AVAHI_PROTO_UNSPEC;
	switch (address.GetFamily()) {
	case AF_INET:
		protocol = AVAHI_PROTO_INET;
		break;

	case AF_INET6:
		/* don't restrict to AVAHI_PROTO_INET6 if IPv4
		   connections are possible (i.e. this is a wildcard
		   listener and v6only disabled) */
		if (v6only || !IPv6Address::Cast(address).IsAny())
			protocol = AVAHI_PROTO_INET6;
		break;
	}

	AddService(ii, protocol, type, port);
}

inline void
Publisher::GroupCallback(AvahiEntryGroup *g,
			 AvahiEntryGroupState state) noexcept
{
	switch (state) {
	case AVAHI_ENTRY_GROUP_ESTABLISHED:
		break;

	case AVAHI_ENTRY_GROUP_COLLISION:
		if (!visible_services)
			/* meanwhile, HideServices() has been called */
			return;

		/* pick a new name */

		{
			char *new_name = avahi_alternative_service_name(name.c_str());
			name = new_name;
			avahi_free(new_name);
		}

		/* And recreate the services */
		RegisterServices(avahi_entry_group_get_client(g));
		break;

	case AVAHI_ENTRY_GROUP_FAILURE:
		logger(3, "Avahi service group failure: ",
		       avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
		break;

	case AVAHI_ENTRY_GROUP_UNCOMMITED:
	case AVAHI_ENTRY_GROUP_REGISTERING:
		break;
	}
}

void
Publisher::GroupCallback(AvahiEntryGroup *g,
			 AvahiEntryGroupState state,
			 void *userdata) noexcept
{
	auto &publisher = *(Publisher *)userdata;
	publisher.GroupCallback(g, state);
}

void
Publisher::RegisterServices(AvahiClient *c) noexcept
{
	assert(visible_services);

	if (group == nullptr) {
		group = avahi_entry_group_new(c, GroupCallback, this);
		if (group == nullptr) {
			logger(3, "Failed to create Avahi service group: ",
			       avahi_strerror(avahi_client_errno(c)));
			return;
		}
	}

	for (const auto &i : services) {
		int error = avahi_entry_group_add_service(group,
							  i.interface,
							  i.protocol,
							  AvahiPublishFlags(0),
							  name.c_str(), i.type.c_str(),
							  nullptr, nullptr,
							  i.port, nullptr);
		if (error < 0) {
			logger(3, "Failed to add Avahi service ", i.type.c_str(),
			       ": ", avahi_strerror(error));
			return;
		}
	}

	int result = avahi_entry_group_commit(group);
	if (result < 0) {
		logger(3, "Failed to commit Avahi service group: ",
		       avahi_strerror(result));
		return;
	}
}

void
Publisher::HideServices() noexcept
{
	if (!visible_services)
		return;

	visible_services = false;

	if (group != nullptr) {
		avahi_entry_group_free(group);
		group = nullptr;
	}
}

void
Publisher::ShowServices() noexcept
{
	if (visible_services)
		return;

	visible_services = true;

	if (services.empty() || group != nullptr)
		return;

	auto *c = client.GetClient();
	if (c != nullptr)
		RegisterServices(c);
}

void
Publisher::OnAvahiConnect(AvahiClient *c) noexcept
{
	if (!services.empty() && group == nullptr && visible_services)
		RegisterServices(c);
}

void
Publisher::OnAvahiDisconnect() noexcept
{
	if (group != nullptr) {
		avahi_entry_group_free(group);
		group = nullptr;
	}
}

void
Publisher::OnAvahiChanged() noexcept
{
	if (group != nullptr)
		avahi_entry_group_reset(group);
}

} // namespace Avahi