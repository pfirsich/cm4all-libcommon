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

#include "CgroupOptions.hxx"
#include "CgroupState.hxx"
#include "AllocatorPtr.hxx"
#include "system/Error.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/WriteFile.hxx"
#include "util/StringAPI.hxx"
#include "util/StringView.hxx"
#include "util/RuntimeError.hxx"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef __linux__
#error This library requires Linux
#endif

CgroupOptions::CgroupOptions(AllocatorPtr alloc,
			     const CgroupOptions &src) noexcept
	:name(alloc.CheckDup(src.name))
{
	auto set_tail = set.before_begin();

	for (const auto &i : src.set) {
		auto *new_set = alloc.New<SetItem>(alloc.Dup(i.name),
						   alloc.Dup(i.value));
		set_tail = set.insert_after(set_tail, *new_set);
	}
}

void
CgroupOptions::Set(AllocatorPtr alloc,
		   StringView _name, StringView _value) noexcept
{
	auto *new_set = alloc.New<SetItem>(alloc.DupZ(_name), alloc.DupZ(_value));
	set.push_front(*new_set);
}

static void
WriteFile(FileDescriptor fd, const char *path, std::string_view data)
{
	if (TryWriteExistingFile(fd, path, data) == WriteFileResult::ERROR)
		throw FormatErrno("write('%s') failed", path);
}

/**
 * Create a new cgroup and return an O_PATH file descriptor to it.
 */
static UniqueFileDescriptor
MakeCgroup(const FileDescriptor group_fd, const char *sub_group)
{
	if (mkdirat(group_fd.Get(), sub_group, 0777) < 0) {
		switch (const int e = errno) {
		case EEXIST:
			break;

		default:
			throw FormatErrno(e, "mkdir('%s') failed", sub_group);
		}
	}

	return OpenPath(group_fd, sub_group);
}

static UniqueFileDescriptor
MkdirOpenPath(const FileDescriptor parent_fd,
	      const char *name, mode_t mode=0777)
{
	if (mkdirat(parent_fd.Get(), name, mode) < 0) {
		switch (errno) {
		case EEXIST:
			break;

		default:
			throw FormatErrno("mkdir('%s') failed",
					  name);
		}
	}

	return OpenPath(parent_fd, name);
}

static UniqueFileDescriptor
MoveToNewCgroup(const FileDescriptor group_fd,
		const char *sub_group, const char *session_group,
		std::string_view pid)
{
	auto fd = MakeCgroup(group_fd, sub_group);

	if (session_group != nullptr) {
		auto session_fd = MkdirOpenPath(fd, session_group);
		WriteFile(session_fd, "cgroup.procs", pid);
	} else
		WriteFile(fd, "cgroup.procs", pid);

	return fd;
}

void
CgroupOptions::Apply(const CgroupState &state, unsigned _pid) const
{
	if (name == nullptr)
		return;

	if (!state.IsEnabled())
		throw std::runtime_error("Control groups are disabled");

	char pid_buffer[32];
	const std::string_view pid(pid_buffer,
				   sprintf(pid_buffer, "%u", _pid));

	std::map<std::string, UniqueFileDescriptor> fds;
	for (const auto &mount : state.mounts)
		fds[mount.name] =
			MoveToNewCgroup(mount.fd, name, session, pid);

	for (const auto &s : set) {
		const char *filename = s.name;

		const char *dot = strchr(filename, '.');
		assert(dot != nullptr);

		const std::string_view controller{filename, dot};
		const auto i = state.controllers.find(controller);
		if (i == state.controllers.end())
			throw FormatRuntimeError("cgroup controller '%.*s' is unavailable",
						 int(controller.size()),
						 controller.data());

		const std::string &mount_point = i->second;

		const auto j = fds.find(mount_point);
		assert(j != fds.end());

		/* emulate cgroup1 for old translation servers */
		if (state.memory_v2 &&
		    StringIsEqual(filename, "memory.limit_in_bytes"))
			filename = "memory.max";

		const FileDescriptor fd = j->second;
		WriteFile(fd, filename, s.value);
	}
}

char *
CgroupOptions::MakeId(char *p) const noexcept
{
	if (name != nullptr) {
		p = (char *)mempcpy(p, ";cg", 3);
		p = stpcpy(p, name);

		if (session != nullptr) {
			*p++ = '/';
			p = stpcpy(p, session);
		}
	}

	return p;
}
