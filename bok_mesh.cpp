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
// When a point is inserted into the mesh, it gets a __mesh element
// added to it.  Do not touch this.
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
//  move(self, pt)	-- pt has moved; update the mesh
//
//  points(self)	-- return a list of all points
//
//  adjacent(self, pt)  -- return a list of points connected to pt
//  edges(self, [pt])	-- return a list of edges attached to pt (or all)
//  triangles(self, [pt])-- return a list of triangles with pt as a vertex (or all)
//
//  stab(self, pt)	-- return the triangle containing pt (if pt is a vertex
//			   of multiple triangles then it will return any of
//			   those triangles).  pt need not be a mesh vertex.
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

static void pushref(lua_State *L, int ref)
{
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
}

// Decorate at table at idx with a lightuserdata pointer
static void decorate(lua_State *L, int idx, void *userdata)
{
	idx = absidx(L, idx);

	lua_pushliteral(L, "__mesh");
	lua_pushlightuserdata(L, userdata);
	lua_settable(L, idx);
}

static void undecorate(lua_State *L, int idx)
{
	if (lua_istable(L, idx)) {
		idx = absidx(L, idx);

		lua_pushliteral(L, "__mesh");
		lua_pushnil(L);
		lua_settable(L, idx);
	}
}

static void *getdecoration(lua_State *L, int idx)
{
	void *ret;

	idx = absidx(L, idx);
	
	lua_pushliteral(L, "__mesh");
	lua_gettable(L, idx);
	ret = lua_touserdata(L, -1);
	lua_pop(L, 1);

	return ret;
}

/****************************************
 BokVertex
 ****************************************/

typedef struct _BokVertex         BokVertex;

struct _BokVertex {
	GtsVertex parent;

	lua_State *L;		// Lua interpreter
	int ref;		// reference to Lua data
};

#define BOK_VERTEX(obj)            GTS_OBJECT_CAST (obj,\
					         BokVertex,\
					         bok_vertex_class ())
#define IS_BOK_VERTEX(obj)         (gts_object_is_from_class (obj,\
						 bok_vertex_class ()))

static GtsVertexClass *bok_vertex_class  (void);

/* BokVertex: Object */

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
	lua_State *L = v->L;

	if (L) {
		// remove decoration
		pushref(L, v->ref);
		undecorate(L, -1);
		lua_pop(L, 1);

		// drop reference
		luaL_unref(v->L, LUA_REGISTRYINDEX, v->ref);
	}

	v->L = NULL;
	v->ref = LUA_NOREF;

	(* GTS_OBJECT_CLASS (bok_vertex_class ())->parent_class->destroy) (object);
}


static void bok_vertex_class_init (GtsVertexClass * klass)
{
	GTS_OBJECT_CLASS(klass)->clone = bok_vertex_clone;
	GTS_OBJECT_CLASS(klass)->destroy = bok_vertex_destroy;
}

static void bok_vertex_init (BokVertex * v)
{
	v->L = NULL;
	v->ref = LUA_NOREF;
}

GtsVertexClass * bok_vertex_class (void)
{
	static GtsVertexClass * klass = NULL;

	if (klass == NULL) {
		GtsObjectClassInfo bok_vertex_info = {
			"BokVertex",
			sizeof (BokVertex),
			sizeof (GtsVertexClass),
			(GtsObjectClassInitFunc) bok_vertex_class_init,
			(GtsObjectInitFunc) bok_vertex_init,
			(GtsArgSetFunc) NULL,
			(GtsArgGetFunc) NULL
		};
		klass = GTS_VERTEX_CLASS(gts_object_class_new (GTS_OBJECT_CLASS (gts_vertex_class ()),
							       &bok_vertex_info));
	}

	return klass;
}

static BokVertex *bok_vertex_new(GtsVertexClass *klass, lua_State *L, int idx,
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

/****************************************
 BokEdge
 ****************************************/

typedef struct _BokEdge         BokEdge;

struct _BokEdge {
	/*< private >*/
	GtsEdge parent;

	/*< public >*/
	lua_State *L;
	int ref;		// reference to lua edge (edge)
};

#define BOK_EDGE(obj)            GTS_OBJECT_CAST (obj,\
						  BokEdge,		\
						  bok_edge_class ())
#define IS_BOK_EDGE(obj)         (gts_object_is_from_class (obj,\
							       bok_edge_class ()))

GtsEdgeClass * bok_edge_class  (void);
BokEdge * bok_edge_new    (GtsEdgeClass * klass);

static void bok_edge_init (BokEdge * object)
{
	object->L = NULL;
	object->ref = LUA_NOREF;
}

static void bok_edge_clone(GtsObject *clone, GtsObject *object)
{
	(* GTS_OBJECT_CLASS (bok_edge_class ())->parent_class->clone) (clone, 
									  object);
	BOK_EDGE (clone)->L = NULL;
	BOK_EDGE (clone)->ref = LUA_NOREF;
}

static void bok_edge_destroy (GtsObject * object)
{
	BokEdge *e = BOK_EDGE(object);
	lua_State *L = e->L;

	if (L) {
		// remove decoration
		pushref(L, e->ref);
		undecorate(L, -1);
		lua_pop(L, 1);
		
		// drop reference
		luaL_unref(L, LUA_REGISTRYINDEX, e->ref);
	}

	e->L = NULL;
	e->ref = LUA_NOREF;

	(* GTS_OBJECT_CLASS (bok_edge_class ())->parent_class->destroy) (object);
}

static void bok_edge_class_init (GtsEdgeClass * klass)
{
	GTS_OBJECT_CLASS(klass)->clone = bok_edge_clone;
	GTS_OBJECT_CLASS(klass)->destroy = bok_edge_destroy;
}

GtsEdgeClass * bok_edge_class (void)
{
	static GtsEdgeClass * klass = NULL;

	if (klass == NULL) {
		GtsObjectClassInfo bok_edge_info = {
			"BokEdge",
			sizeof (BokEdge),
			sizeof (GtsEdgeClass),
			(GtsObjectClassInitFunc) bok_edge_class_init,
			(GtsObjectInitFunc) bok_edge_init,
			(GtsArgSetFunc) NULL,
			(GtsArgGetFunc) NULL
		};
		klass = GTS_EDGE_CLASS(gts_object_class_new (GTS_OBJECT_CLASS (gts_edge_class ()),
							     &bok_edge_info));
	}

	return klass;
}

BokEdge * bok_edge_new (GtsEdgeClass * klass)
{
	BokEdge * object;

	object = BOK_EDGE (gts_object_new (GTS_OBJECT_CLASS (klass)));

	return object;
}



/****************************************/


static int mesh_gc(lua_State *);

static struct mesh *mesh_get(lua_State *L, int idx)
{
	return (struct mesh *)luaL_checkudata(L, idx, "bokchoi.mesh");
}

static int mesh_new(lua_State *L)
{
	struct mesh *mesh;
	int nargs = lua_gettop(L);

	mesh = (struct mesh *)lua_newuserdata(L, sizeof(*mesh));

	luaL_getmetatable(L, "bokchoi.mesh");
	lua_setmetatable(L, -2);

	mesh->surface = gts_surface_new(gts_surface_class(),
					gts_face_class(),
					bok_edge_class(),
					bok_vertex_class());

	// Build a list of bounding points to create a hull-face for
	// the surface
	GSList *list = NULL;
	int points = 0;

	for(int i = 2; i <= nargs; i++) {
		float x, y;

		if (!get_xy(L, i, &x, &y))
			continue;

		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), x, y, 0));
		points++;
	}
	
	if (points == 0) {
		// default bounding box is (-1000,-1000)(1000,1000)
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), -1000, -1000, 0));
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), 1000, 1000, 0));
	} else if (points == 1) {
		// at least include 0,0
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), 0, 0, 0));
	}

	GtsTriangle *tri = gts_triangle_enclosing(gts_triangle_class(), list, 100.);

	for(GSList *p = list; p != NULL; p = p->next)
		gts_object_destroy(GTS_OBJECT(p->data));
	g_slist_free(list);

	gts_surface_add_face(mesh->surface, gts_face_new(gts_face_class(), 
							 tri->e1, tri->e2, tri->e3));
	gts_object_destroy(GTS_OBJECT(tri));

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
	int narg = lua_gettop(L);
	struct mesh *mesh = mesh_get(L, 1);
	
	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	for(int i = 2; i <= narg; i++) {
		float x, y;
		BokVertex *v;
		GtsVertex *v1;

		if (!get_xy(L, i, &x, &y))
			continue;
		
		//printf("mesh.add: mesh=%p, %g,%g\n", mesh, x, y);

		v = bok_vertex_new(bok_vertex_class(), L, i, x, y);
		v1 = gts_delaunay_add_vertex(mesh->surface, GTS_VERTEX(v), NULL);

		// If it returns the same point we fed it, then it
		// means it was rejected.
		if (v1 == GTS_VERTEX(v)) {
			gts_object_destroy(GTS_OBJECT(v));
			continue;
		}
			
		if (v1 != NULL) {
			// another point is already here; replace it
			printf("replacing v1=%p with v=%p\n",
			       v1, v);
			// gts_vertex_replace doesn't seem to work well
			// gts_vertex_replace(v1, GTS_VERTEX(v));
			gts_delaunay_remove_vertex(mesh->surface, v1);

			v1 = gts_delaunay_add_vertex(mesh->surface, GTS_VERTEX(v), NULL);
			assert(v1 == NULL);
		}

		// add/replace __mesh reference in point
		decorate(L, i, v);

		//gts_surface_print_stats(mesh->surface, stdout);
	}
		
	return 0;
}

static int mesh_del(lua_State *L)
{
	int narg = lua_gettop(L);
	struct mesh *mesh = mesh_get(L, 1);
	
	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	// look for each arg; we expect to find points with
	// lightuserdata pointers to the corresponding BokVertex.
	for(int i = 2; i <= narg; i++) {
		void *d;

		if (!lua_istable(L, i))
			continue;

		d = getdecoration(L, i);

		if (d && GTS_IS_VERTEX(d)) {
			GtsVertex *v = GTS_VERTEX(d);
			
			if (v != NULL) {
				if (0)
					printf("mesh.del: mesh=%p, %g,%g\n", 
					       mesh, v->p.x, v->p.y);

				gts_delaunay_remove_vertex(mesh->surface, v);
				//gts_surface_print_stats(mesh->surface, stdout);
			}
		}

		undecorate(L, i);
	}
	
	return 0;
}

struct surface_foreach {
	lua_State *L;
	int count;
};

static gint foreach_vertex_func(gpointer item, gpointer data)
{
	struct surface_foreach *vdata = (struct surface_foreach *)data;

	if (IS_BOK_VERTEX(item)) {
		BokVertex *v = BOK_VERTEX(item);

		assert(vdata->L == v->L);

		// push the referenced vertex to stack for return
		lua_rawgeti(vdata->L, LUA_REGISTRYINDEX, v->ref);
		vdata->count++;
	}

	return 0;
}

static int mesh_points(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	struct surface_foreach data = { L, 0 };

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	gts_surface_foreach_vertex(mesh->surface, foreach_vertex_func, &data);

	return data.count;
}

// Construct a new edge table. This consists of two verticies at
// indicies 1 and 2, a __mesh entry which points back to the BokEdge,
// and a metatable (TODO).
static void make_edge(lua_State *L, BokEdge *e, BokVertex *v1, BokVertex *v2)
{
	assert(e->L  == L);
	assert(v1->L == L);
	assert(v2->L == L);

	lua_newtable(L);

	pushref(L, v1->ref);
	lua_rawseti(L, -2, 1);

	pushref(L, v2->ref);
	lua_rawseti(L, -2, 2);

	decorate(L, -1, e);

	lua_pushvalue(L, -1);
	e->ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

static gint foreach_edge_func(gpointer item, gpointer data)
{
	struct surface_foreach *edata = (struct surface_foreach *)data;
	
	if (IS_BOK_EDGE(item) && GTS_IS_SEGMENT(item)) {
		BokEdge *e = BOK_EDGE(item);
		GtsSegment *s = GTS_SEGMENT(item);
		lua_State *L = edata->L;

		lua_checkstack(L, edata->count+5);

		if (e->ref != LUA_NOREF) {
			pushref(L, e->ref);
			edata->count++;
		} else 	if (IS_BOK_VERTEX(s->v1) && IS_BOK_VERTEX(s->v2)) {
			e->L = edata->L;
			make_edge(L, e, BOK_VERTEX(s->v1), BOK_VERTEX(s->v2));
			edata->count++;
		}
	}

	return 0;
}

static int mesh_edges(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	int nargs = lua_gettop(L);
	int ret = 0;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	if (lua_istable(L, 2)) {
		void *d = getdecoration(L, 2);

		if (d && GTS_IS_VERTEX(d)) {
			// find edges attached to this point
			GtsVertex *v = GTS_VERTEX(d);
		}
	} else {
		// return everything
		struct surface_foreach data = { L, 0 };

		gts_surface_foreach_edge(mesh->surface, foreach_edge_func, &data);
		ret = data.count;
	}

	return ret;
}

// taken from cdt.c; check that a face is still in a consistent triangulation
static bool delaunay_check (GtsSurface *surface, GtsTriangle * t)
{
	GSList * i, * list;
	GtsVertex * v1, * v2, * v3;
	bool ret = true;

	gts_triangle_vertices (t, &v1, &v2, &v3);
	list = gts_vertex_neighbors (v1, NULL, surface);
	list = gts_vertex_neighbors (v2, list, surface);
	list = gts_vertex_neighbors (v3, list, surface);
	i = list;
	for (i = list; i; i = i->next) {
		GtsVertex * v = GTS_VERTEX(i->data);
		if (v != v1 && v != v2 && v != v3 &&
		    gts_point_in_circle (GTS_POINT (v), 
					 GTS_POINT (v1),
					 GTS_POINT (v2),  
					 GTS_POINT (v3)) > 0.) {
			ret = false;
			break;
		}
	}
	g_slist_free (list);

	return ret;
}

// Update the mesh if a point moves.
//
// XXX This is pretty useless; it will remove and replace the edges
// even if the mesh topology is unchanged, which destroys any edge
// annotations.  At the moment this looks to see if the moved point
// violates the delaunay constraint, and if so removes it and re-adds
// it.  This will remove and replace edges which end up being
// unchanged.
static int mesh_move(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	void *d;
	float x, y;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");
	
	d = getdecoration(L, 2);

	luaL_argcheck(L, d != NULL && IS_BOK_VERTEX(d) && get_xy(L, 2, &x, &y),
		      2, "vertex point expected");

	BokVertex *v = BOK_VERTEX(d);
	GtsPoint *p = GTS_POINT(v);

	p->x = x;
	p->y = y;

	GSList *list = gts_vertex_faces(GTS_VERTEX(v), mesh->surface, NULL);

	for(GSList *l = list; l != NULL; l = l->next) {
		GtsFace *f = GTS_FACE(l->data);

		// If moving the points breaks the delaunay property, then
		// remove and reinsert it.
		if (!delaunay_check(mesh->surface, GTS_TRIANGLE(f))) {
			gts_allow_floating_vertices = TRUE;

			gts_delaunay_remove_vertex(mesh->surface, GTS_VERTEX(v));
			gts_delaunay_add_vertex(mesh->surface, GTS_VERTEX(v), NULL);

			gts_allow_floating_vertices = FALSE;			
			break;
		}
	}

	g_slist_free(list);

	return 0;
}

static const luaL_reg mesh_meta[] = {
	{ "__gc",	mesh_gc },
	{ "add",	mesh_add },
	{ "del",	mesh_del },

	{ "points",	mesh_points },

	{ "move",	mesh_move },

	{ "edges",	mesh_edges },

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
