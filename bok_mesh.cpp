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
#include <assert.h>

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

typedef struct _BokVertex {
	GtsVertex v;

	lua_State *L;		// Lua interpreter
	int ref;		// reference to Lua data
} BokVertex;

typedef struct _BokVertexClass {
	GtsVertexClass parent_class;
} BokVertexClass;

static BokVertexClass *bok_vertex_class(void);

#define BOK_VERTEX(obj)             GTS_OBJECT_CAST (obj,	\
						     BokVertex,		\
						     bok_vertex_class ())

static void bok_vertex_init(BokVertex *v)
{
	v->L = NULL;
	v->ref = LUA_NOREF;
}

static void bok_vertex_clone(GtsObject *clone, GtsObject *object)
{
	(* GTS_OBJECT_CLASS (bok_vertex_class ())->parent_class->clone) (clone, 
									 object);
	BOK_VERTEX (clone)->L = NULL;
	BOK_VERTEX (clone)->ref = LUA_NOREF;
}

static void bok_vertex_destroy (GtsObject * object)
{
	BokVertex *v = BOK_VERTEX(object);

	luaL_unref(v->L, LUA_REGISTRYINDEX, v->ref);
	v->L = NULL;
	v->ref = LUA_NOREF;

	(* GTS_OBJECT_CLASS (bok_vertex_class ())->parent_class->destroy) (object);
}

static void bok_vertex_class_init(BokVertexClass *klass)
{
	GTS_OBJECT_CLASS(klass)->clone = bok_vertex_clone;
	GTS_OBJECT_CLASS(klass)->destroy = bok_vertex_destroy;
}

static BokVertexClass *bok_vertex_class(void)
{
	static BokVertexClass *klass = NULL;

	if (klass == NULL) {
		GtsObjectClassInfo vertex_info = {
			"BokVertex",
			sizeof (BokVertex),
			sizeof (BokVertexClass),
			(GtsObjectClassInitFunc) bok_vertex_class_init,
			(GtsObjectInitFunc) bok_vertex_init,
			(GtsArgSetFunc) NULL,
			(GtsArgGetFunc) NULL
		};
		klass = (BokVertexClass *)gts_object_class_new (GTS_OBJECT_CLASS (gts_vertex_class ()), 
								&vertex_info);
	}
	
	return klass;
}

static BokVertex *bok_vertex_new(BokVertexClass *klass, lua_State *L, int idx,
				 float x, float y)
{
	BokVertex *v;

	v = BOK_VERTEX(gts_object_new(GTS_OBJECT_CLASS(klass)));

	v->L = L;
	lua_pushvalue(L, idx);
	v->ref = luaL_ref(L, LUA_REGISTRYINDEX);

	gts_point_set(GTS_POINT(v), x, y, 0);

	return v;
}

static int mesh_gc(lua_State *);

static struct mesh *mesh_get(lua_State *L, int idx)
{
	return (struct mesh *)luaL_checkudata(L, idx, "bokchoi.mesh");
}

static int mesh_new(lua_State *L)
{
	struct mesh *mesh;

	mesh = (struct mesh *)lua_newuserdata(L, sizeof(*mesh));

	luaL_getmetatable(L, "bokchoi.mesh");
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
	struct mesh *mesh = mesh_get(L, 1);
	int narg = lua_gettop(L);

	printf("mesh.add: mesh=%p, narg=%d\n", mesh, narg);

	for(int i = 2; i <= narg; i++) {
		float x, y;
		BokVertex *v, *v1;

		if (!lua_istable(L, i))
			continue;

		lua_pushliteral(L, "x");
		lua_gettable(L, i);
		x = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_pushliteral(L, "y");
		lua_gettable(L, i);
		y = lua_tonumber(L, -1);
		lua_pop(L, 1);

		printf("adding %g,%g to mesh\n", x, y);

		v = bok_vertex_new(bok_vertex_class(), L, i, x, y);
		v1 = BOK_VERTEX(gts_delaunay_add_vertex(mesh->surface, GTS_VERTEX(v), NULL));
		assert(v1 != v);

		if (v1 != NULL) {
			// another point is already here; replace it
			gts_vertex_replace(GTS_VERTEX(v1), GTS_VERTEX(v));
		}

		// add/replace __mesh reference in point
		lua_pushliteral(L, "__mesh");
		lua_pushlightuserdata(L, v);
		lua_settable(L, i);
	}
	
	
	return 0;
}

static int mesh_del(lua_State *L)
{
	return 0;
}

static const luaL_reg mesh_meta[] = {
	{ "__gc",	mesh_gc },
	{ "add",	mesh_add },
	{ "del",	mesh_del },

	{0,0}
};

static const luaL_reg mesh_methods[] = {
	{ "new",	mesh_new },

	{0,0}
};

void mesh_register(lua_State *L)
{
	luaL_openlib(L, "mesh", mesh_methods, 0);

	luaL_newmetatable(L, "bokchoi.mesh");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);  /* pushes the metatable */
	lua_settable(L, -3);  /* metatable.__index = metatable */
  
	luaL_openlib(L, 0, mesh_meta, 0);

	lua_pop(L, 2);
}
