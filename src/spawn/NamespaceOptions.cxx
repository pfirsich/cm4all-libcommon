// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "NamespaceOptions.hxx"
#include "UserNamespace.hxx"
#include "PidNamespace.hxx"
#include "NetworkNamespace.hxx"
#include "UidGid.hxx"
#include "AllocatorPtr.hxx"
#include "system/Error.hxx"

#include <set>

#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>

NamespaceOptions::NamespaceOptions(AllocatorPtr alloc,
				   const NamespaceOptions &src)
	:enable_user(src.enable_user),
	 enable_pid(src.enable_pid),
	 enable_cgroup(src.enable_cgroup),
	 enable_network(src.enable_network),
	 enable_ipc(src.enable_ipc),
	 pid_namespace(alloc.CheckDup(src.pid_namespace)),
	 network_namespace(alloc.CheckDup(src.network_namespace)),
	 hostname(alloc.CheckDup(src.hostname)),
	 mount(alloc, src.mount)
{
}

#if TRANSLATION_ENABLE_EXPAND

bool
NamespaceOptions::IsExpandable() const
{
	return mount.IsExpandable();
}

void
NamespaceOptions::Expand(AllocatorPtr alloc, const MatchData &match_data)
{
	mount.Expand(alloc, match_data);
}

#endif

uint_least64_t
NamespaceOptions::GetCloneFlags(uint_least64_t flags) const noexcept
{
	if (enable_user)
		flags |= CLONE_NEWUSER;
	if (enable_pid && pid_namespace == nullptr)
		flags |= CLONE_NEWPID;
	if (enable_cgroup)
		flags |= CLONE_NEWCGROUP;
	if (enable_network)
		flags |= CLONE_NEWNET;
	if (enable_ipc)
		flags |= CLONE_NEWIPC;
	if (mount.IsEnabled())
		flags |= CLONE_NEWNS;
	if (hostname != nullptr)
		flags |= CLONE_NEWUTS;

	return flags;
}

void
NamespaceOptions::SetupUidGidMap(const UidGid &uid_gid, int pid) const
{
	/* collect all gids (including supplementary groups) in a std::set
	   to eliminate duplicates, and then map them all into the new
	   user namespace */
	std::set<unsigned> gids;
	gids.emplace(uid_gid.gid);
	for (unsigned i = 0; uid_gid.groups[i] != 0; ++i)
		gids.emplace(uid_gid.groups[i]);

	SetupGidMap(pid, gids);
	SetupUidMap(pid, uid_gid.uid, false);
}

void
NamespaceOptions::ReassociatePid() const
{
	assert(pid_namespace != nullptr);

	ReassociatePidNamespace(pid_namespace);
}

void
NamespaceOptions::ReassociateNetwork() const
{
	assert(network_namespace != nullptr);

	ReassociateNetworkNamespace(network_namespace);
}

void
NamespaceOptions::Apply(const UidGid &uid_gid) const
{
	/* set up UID/GID mapping in the old /proc */
	if (enable_user) {
		DenySetGroups(0);

		if (uid_gid.gid != 0)
			SetupGidMap(0, uid_gid.gid, false);
		// TODO: map the current effective gid if no gid was given?

		SetupUidMap(0, uid_gid.uid, false);
	}

	if (network_namespace != nullptr)
		ReassociateNetwork();

	mount.Apply(uid_gid);

	if (hostname != nullptr &&
	    sethostname(hostname, strlen(hostname)) < 0)
		throw MakeErrno("sethostname() failed");
}

void
NamespaceOptions::ApplyNetwork() const
{
	if (network_namespace != nullptr)
		ReassociateNetwork();
	else if (enable_network) {
		if (unshare(CLONE_NEWNET) < 0)
			throw MakeErrno("unshare(CLONE_NEWNET) failed");
	}
}

char *
NamespaceOptions::MakeId(char *p) const
{
	if (enable_user)
		p = (char *)mempcpy(p, ";uns", 4);

	if (enable_pid)
		p = (char *)mempcpy(p, ";pns", 4);

	if (pid_namespace != nullptr) {
		p = (char *)mempcpy(p, ";pns=", 5);
		p = (char *)stpcpy(p, pid_namespace);
	}

	if (enable_cgroup)
		p = (char *)mempcpy(p, ";cns", 4);

	if (enable_network) {
		p = (char *)mempcpy(p, ";nns", 4);

		if (network_namespace != nullptr) {
			*p++ = '=';
			p = stpcpy(p, network_namespace);
		}
	}

	if (enable_ipc)
		p = (char *)mempcpy(p, ";ins", 4);

	p = mount.MakeId(p);

	if (hostname != nullptr) {
		p = (char *)mempcpy(p, ";uts=", 5);
		p = stpcpy(p, hostname);
	}

	return p;
}
