// 
// Lua binding to a GTS mesh object
//
// A mesh is a Delaunay triangulation of a set of points, which become
// verticies in the mesh.  As far as we're concerned, a point is any
// Lua table which has an 'x' and 'y' element.
//
// To do this, we subclass the GTS Vertex type to include a reference
// to the Lua point table, so that the meshes refer to the Lua points.
//
// XXX Hm, is this even necessary?  We could simply produce a
// structure with all the points in it, and the appropriate methods
// for traversing it, without the need for a persistent userdata mesh
// instance.  But that would require re-implementing pieces of GTS,
// and would prevent incremental modification of the mesh...
//
// Library: mesh
// Functions:
//	new([points...]) - return a new mesh structure
//
// Object: mesh
// Methods:
//  add(self, pt)	-- add a point to the mesh
//  del(self, pt)	-- remove a point from the mesh
//
//  triangulate(self)   -- connect points into a mesh 
//
//  adjacent(self, pt)  -- return a list of points connected to pt
//  edges(self, pt)	-- return a list of edges attached to pt
//  triangles(self, pt) -- return a list of triangles containing pt
//
// Object: edge
//  points(self)	-- return tuple of points containing the edge
//  triangles(self)	-- return the triangles on either side of 
//                         edge (or nil if one is missing)
// Object: triangle
//  points(self)	-- return triple of points
//  edges(self)		-- return triple of edges
//  neighbours(self)	-- return triple of neighbouring triangles

#include <string.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <gts.h>
}

#include "bok_lua.h"

struct mesh
{
	GtsSurface *surface;
};

static int mesh_gc(lua_State *);

static struct mesh *mesh_get(lua_State *L, int idx)
{
	return (struct mesh *)userdata_get(L, idx, mesh_gc, "mesh");
}

static int mesh_new(lua_State *L)
{
	struct mesh *mesh;

	mesh = (struct mesh *)lua_newuserdata(L, sizeof(*mesh));

	luaL_getmetatable(L, "mesh");
	lua_setmetatable(L, -2);

	mesh->surface = gts_surface_new(gts_surface_class(),
					gts_face_class(),
					gts_edge_class(),
					gts_vertex_class());

	return 1;
}

static int mesh_gc(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);

	gts_object_destroy(GTS_OBJECT(mesh->surface));

	return 0;
}

static int mesh_add(lua_State *L)
{
	return 0;
}

static int mesh_del(lua_State *L)
{
	return 0;
}

static int mesh_index(lua_State *L)
{
	static const luaL_reg mesh_methods[] = {
		{ "add",	mesh_add },
		{ "del",	mesh_del },
		
		{0,0}
	};
	const char *str;

	str = lua_tostring(L, 2);
	if (str == NULL)
		luaL_error(L, "mesh index must be string");

	for(int i = 0; mesh_methods[i].name; i++)
		if (strcmp(str, mesh_methods[i].name) == 0) {
			lua_pushcfunction(L, mesh_methods[i].func);
			return 1;
		}

	return 0;
}

static const luaL_reg mesh_meta[] = {
	{ "__gc",	mesh_gc },
	{ "__index",	mesh_index },

	{0,0}
};

static const luaL_reg mesh_methods[] = {
	{ "new",	mesh_new },

	{0,0}
};

void mesh_register(lua_State *L)
{
	luaL_openlib(L, "mesh", mesh_methods, 0);

	luaL_newmetatable(L, "mesh");
	luaL_openlib(L, 0, mesh_meta, 0);

	lua_pop(L, 2);
}
