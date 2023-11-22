// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "XattrTable.hxx"
#include "lua/Class.hxx"
#include "lua/Util.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <string.h>
#include <sys/xattr.h>

namespace Lua {

class XattrTable final {
	UniqueFileDescriptor fd;

public:
	explicit XattrTable(UniqueFileDescriptor &&_fd) noexcept
		:fd(std::move(_fd)) {}

	static int Index(lua_State *L);

private:
	int _Index(lua_State *L, const char *name);
};

static constexpr char lua_xattr_table_class[] = "io.XattrTable";
using XattrTableClass = Lua::Class<XattrTable, lua_xattr_table_class>;

int
XattrTable::Index(lua_State *L)
{
	if (lua_gettop(L) != 2)
		return luaL_error(L, "Invalid parameters");

	return XattrTableClass::Cast(L, 1)._Index(L, luaL_checkstring(L, 2));
}

inline int
XattrTable::_Index(lua_State *L, const char *name)
{
	char buffer[4096];
	ssize_t nbytes;

	if (strchr(name, '.') == nullptr && strlen(name) < 1024) {
		/* if there is no dot, prepend the "user." attribute
		   namespace */
		char name_buffer[1100];
		strcpy(stpcpy(name_buffer, "user."), name);

		nbytes = fgetxattr(fd.Get(), name_buffer,
				   buffer, sizeof(buffer));
	} else {
		/* if there is a dot, we assume the caller has
		   specified an attribute namespace */
		nbytes = fgetxattr(fd.Get(), name, buffer, sizeof(buffer));
	}

	if (nbytes < 0)
		return 0;

	Push(L, std::string_view{buffer, static_cast<std::size_t>(nbytes)});
	return 1;
}

void
InitXattrTable(lua_State *L) noexcept
{
	XattrTableClass::Register(L);
	SetField(L, RelativeStackIndex{-1}, "__index", XattrTable::Index);
	lua_pop(L, 1);
}

void
NewXattrTable(struct lua_State *L, UniqueFileDescriptor fd) noexcept
{
	XattrTableClass::New(L, std::move(fd));
}

} // namespace Lua
