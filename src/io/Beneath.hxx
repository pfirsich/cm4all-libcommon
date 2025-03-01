// SPDX-License-Identifier: BSD-2-Clause
// author: Max Kellermann <max.kellermann@gmail.com>

#pragma once

struct FileAt;
class UniqueFileDescriptor;

UniqueFileDescriptor
TryOpenReadOnlyBeneath(FileAt file) noexcept;

UniqueFileDescriptor
OpenReadOnlyBeneath(FileAt file);

UniqueFileDescriptor
TryOpenDirectoryBeneath(FileAt file) noexcept;

UniqueFileDescriptor
OpenDirectoryBeneath(FileAt file);

UniqueFileDescriptor
TryOpenPathBeneath(FileAt file) noexcept;

UniqueFileDescriptor
OpenPathBeneath(FileAt file);
