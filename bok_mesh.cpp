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
//  connected(self, pt) -- return a list of points connected to pt
//  edges(self, [pt])	-- return a list of edges attached to pt (or all)
//  triangles(self, [pt])-- return a list of triangles with pt as a vertex (or all)
//
//  boundary(self)	-- return list of bounding edges
//
//  stab(self, pt)	-- return the triangle containing pt (if pt is a vertex
//			   of multiple triangles then it will return any of
//			   those triangles).  pt need not be a mesh vertex.
//
//  newedge(pt, v1, v2) -- user supplied - if set, called to create an edge
//
//  draw(texture)	-- draw the mesh with texture stretched across it
//			   each point in the mesh can have the following fields:
//				tx,ty: texture coords; x,y used if missing
//				color: vertex colour
//
// Object: edge
//  points(self)	-- return tuple of points containing the edge
//  triangles(self)	-- return the triangles on either side of 
//                         edge (or nil if one is missing)
//  remove(self)	-- user supplied - called when edge is removed from the mesh
//
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
	GtsVertex *boundary[3];	// bounding triangle
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

	int index;		// vertex index in mesh (temporary)
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
	if (BOK_VERTEX(object)->ref != LUA_NOREF) {
		BokVertex *obv = BOK_VERTEX(object);
		BokVertex *nbv = BOK_VERTEX(clone);

		assert(obv->L != NULL);
		assert(nbv->L == obv->L);

		pushref(obv->L, obv->ref);
		nbv->ref = luaL_ref(nbv->L, LUA_REGISTRYINDEX);
	}
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

static GtsEdgeClass * bok_edge_class  (void);
static BokEdge * bok_edge_new    (GtsEdgeClass * klass);

static void bok_edge_init (BokEdge * object)
{
	object->L = NULL;
	object->ref = LUA_NOREF;
}

static void bok_edge_clone(GtsObject *clone, GtsObject *object)
{
	(* GTS_OBJECT_CLASS (bok_edge_class ())->parent_class->clone) (clone, 
								       object);
	if (BOK_EDGE(object)->ref != LUA_NOREF) {
		BokEdge *obe = BOK_EDGE(object);
		BokEdge *nbe = BOK_EDGE(clone);

		assert(obe->L != NULL);
		assert(nbe->L == obe->L);

		pushref(obe->L, obe->ref);
		nbe->ref = luaL_ref(nbe->L, LUA_REGISTRYINDEX);

		// clone vertexes
		GTS_SEGMENT(clone)->v1 = GTS_SEGMENT(object)->v1;
		GTS_SEGMENT(clone)->v2 = GTS_SEGMENT(object)->v2;
	}
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

static BokEdge * bok_edge_new (GtsEdgeClass * klass)
{
	BokEdge * object;

	object = BOK_EDGE (gts_object_new (GTS_OBJECT_CLASS (klass)));

	return object;
}



/****************************************/


// Get the userdata for a mesh object.  If the object is a table, then
// look up a __mesh element which points to the userdata; otherwise
// assume idx is the userdata itself.
static struct mesh *mesh_get(lua_State *L, int idx)
{
	void *ret;
	idx = absidx(L, idx);

	if (lua_istable(L, idx)) {
		lua_pushliteral(L, "__mesh");
		lua_gettable(L, idx);
		ret = luaL_checkudata(L, -1, "bokchoi.meshuserdata");
		lua_pop(L, 1);
	} else
		ret = luaL_checkudata(L, idx, "bokchoi.meshuserdata");

	return (struct mesh *)ret;
}

// This creates a mesh object.  The thing returned is a simple table,
// with a metatable defining all the methods.  The table has a
// '__mesh' element which points to the userdata.
static int mesh_new(lua_State *L)
{
	struct mesh *mesh;
	int nargs = lua_gettop(L);

	lua_newtable(L);	// mesh table to be returned
	luaL_getmetatable(L, "bokchoi.mesh");
	lua_setmetatable(L, -2);

	lua_pushliteral(L, "__mesh");

	mesh = (struct mesh *)lua_newuserdata(L, sizeof(*mesh));
	luaL_getmetatable(L, "bokchoi.meshuserdata");
	lua_setmetatable(L, -2);

	lua_settable(L, -3);	// set ret.__mesh

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
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(),  1000, -1000, 0));
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(),  1000,  1000, 0));
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), -1000,  1000, 0));
	} else if (points == 1) {
		// at least include 0,0
		list = g_slist_prepend(list, gts_vertex_new(gts_vertex_class(), 0, 0, 0));
	}

	GtsTriangle *tri = gts_triangle_enclosing(gts_triangle_class(), list, 100.);

	for(GSList *p = list; p != NULL; p = p->next)
		gts_object_destroy(GTS_OBJECT(p->data));
	g_slist_free(list);

	gts_triangle_vertices(tri, &mesh->boundary[0], &mesh->boundary[1], &mesh->boundary[2]);

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
	int meshidx;
	int setidx;
};

static gint points_func(gpointer item, gpointer data)
{
	struct surface_foreach *vdata = (struct surface_foreach *)data;

	if (IS_BOK_VERTEX(item)) {
		BokVertex *v = BOK_VERTEX(item);

		assert(vdata->L == v->L);

		// push the referenced vertex to stack for return
		pushref(vdata->L, v->ref);
		lua_rawseti(vdata->L, vdata->setidx, ++vdata->count);
	}

	return 0;
}

static int mesh_points(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	int setidx;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	lua_newtable(L);
	setidx = absidx(L, -1);

	struct surface_foreach data = { L, 0, 1, setidx };

	gts_surface_foreach_vertex(mesh->surface,
				   points_func, &data);

	return 1;
}

static int mesh_connected(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	GtsVertex *v = (GtsVertex *)GTS_IS_VERTEX(getdecoration(L, 2));
	GSList *list;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");
	luaL_argcheck(L, v != NULL, 2, "'point' expected");

	list = gts_vertex_neighbors(v, NULL, mesh->surface);

	lua_newtable(L);
	
	for(GSList *p = list; p != NULL; p = p->next) {
		BokVertex *bv = (BokVertex *)IS_BOK_VERTEX(p->data);

		if (bv == NULL)
			continue;

		pushref(L, bv->ref);
		lua_pushvalue(L, -1);
		lua_settable(L, -3);
	}
	g_slist_free(list);

	return 1;
}

// Construct a new edge table. This consists of two verticies at
// indicies 1 and 2, and a __mesh entry which points back to the
// BokEdge.  Leaves the new edge on the top of the stack.
static void make_edge(lua_State *L, int meshidx,
		      BokEdge *e, BokVertex *v1, BokVertex *v2)
{
	assert(e->L  == L);
	assert(v1->L == L);
	assert(v2->L == L);

	meshidx = absidx(L, meshidx);

	if (call_lua(L, 1, meshidx, "newedge", "Irr", meshidx, v1->ref, v2->ref)) {
		if (!lua_istable(L,-1))
			luaL_error(L, "mesh:newedge didn't return an edge");
	} else {
		lua_newtable(L);

		pushref(L, v1->ref);
		lua_rawseti(L, -2, 1);

		pushref(L, v2->ref);
		lua_rawseti(L, -2, 2);
	}

	decorate(L, -1, e);

	lua_pushvalue(L, -1);
	e->ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

// Store an edge in an edge set at setidx; entries are of the form e=e
static void store_lua_edge(lua_State *L, int meshidx, int setidx, BokEdge *e)
{
	GtsSegment *s = GTS_SEGMENT(e);
	bool ok = true;

	meshidx = absidx(L, meshidx);
	setidx = absidx(L, setidx);

	if (e->ref != LUA_NOREF) {
		pushref(L, e->ref);
	} else if (IS_BOK_VERTEX(s->v1) && IS_BOK_VERTEX(s->v2)) {
		e->L = L;
		make_edge(L, meshidx, e,
			  BOK_VERTEX(s->v1), BOK_VERTEX(s->v2));
	} else
		ok = false;

	if (ok) {
		lua_pushvalue(L, -1);
		lua_settable(L, setidx);
	}
}

static gint edges_func(gpointer item, gpointer data)
{
	struct surface_foreach *edata = (struct surface_foreach *)data;
	
	if (IS_BOK_EDGE(item)) {
		assert(GTS_IS_SEGMENT(item));

		store_lua_edge(edata->L, edata->meshidx, edata->setidx,
			      BOK_EDGE(item));
	}

	return 0;
}

static int mesh_edges(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	int narg = lua_gettop(L);
	int setidx;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	lua_newtable(L);
	setidx = absidx(L, -1);

	if (narg >= 2 && lua_istable(L, 2)) {
		void *d = getdecoration(L, 2);

		if (d && IS_BOK_VERTEX(d)) {
			// find edges attached to this point
			GtsVertex *v = GTS_VERTEX(d);

			for(GSList *s = v->segments; 
			    s != NULL; 
			    s = s->next) {
				if (!IS_BOK_EDGE(s->data))
					continue;
				store_lua_edge(L, 1, setidx, 
					       BOK_EDGE(s->data));
			}
		}
	} else {
		// return everything
		struct surface_foreach data = { L, 0, 1, setidx };

		gts_surface_foreach_edge(mesh->surface, 
					 edges_func, &data);
	}

	return 1;
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

// If we've found a vertex which needs removal and replacement,
// remember all the edges it had, and restore the ones which are
// unchanged.
static void adjust_edges(lua_State *L, GtsSurface *surface, BokVertex *v)
{
	gts_allow_floating_vertices = TRUE;

	GSList *segments = NULL;

	// save a list of interesting edges attached to this vertex so
	// we can restore them if needed
	for(GSList *s = GTS_VERTEX(v)->segments; s != NULL; s = s->next) {
		if (IS_BOK_EDGE(s->data) && BOK_EDGE(s->data)->ref != LUA_NOREF)
			segments = g_slist_prepend(segments, 
						   gts_object_clone(GTS_OBJECT(s->data)));
	}

	gts_delaunay_remove_vertex(surface, GTS_VERTEX(v));
	gts_delaunay_add_vertex(surface, GTS_VERTEX(v), NULL);

	// Search segment list for new matching segments, and reattach
	// the Lua state
	for(GSList *s = segments; s != NULL; s = s->next) {
		BokEdge *be = BOK_EDGE(s->data);

		assert(IS_BOK_EDGE(be));
				
		for(GSList *vs = GTS_VERTEX(v)->segments; 
		    vs != NULL; vs = vs->next) {
			BokEdge *ve = BOK_EDGE(vs->data);

			if (IS_BOK_EDGE(vs->data) &&
			    ve->ref == LUA_NOREF &&
			    gts_segments_are_identical(GTS_SEGMENT(be),
						       GTS_SEGMENT(ve))) {
				// transfer ownership of Lua reference
				ve->L = be->L;
				ve->ref = be->ref;
				be->L = NULL;
				be->ref = LUA_NOREF;
			}
		}
		if (be->L != NULL) {
			// Edge was not used; call the edge's remove
			// method (if any)
			assert(be->ref != LUA_NOREF);

			pushref(L, be->ref);
			call_lua(L, 0, -1, "remove", "I", -1);
					
			lua_pop(L, 1);	// drop table
		}

		gts_object_destroy(GTS_OBJECT(be));
	}

	g_slist_free(segments);

	gts_allow_floating_vertices = FALSE;			
}

// Update the mesh when a point moves.
//
// This retains all edges which are unaffected by the move.
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

	// move the point
	p->x = x;
	p->y = y;

	// walk neighbouring faces, and see if any are now illegal
	GSList *list = gts_vertex_faces(GTS_VERTEX(v), mesh->surface, NULL);

	for(GSList *l = list; l != NULL; l = l->next) {
		GtsFace *f = GTS_FACE(l->data);

		// If moving the points breaks the delaunay property, then
		// remove and reinsert it.
		if (!delaunay_check(mesh->surface, GTS_TRIANGLE(f))) {
			adjust_edges(L, mesh->surface, v);
			break;
		}
	}

	g_slist_free(list);

	return 0;
}

static bool make_triangle(lua_State *L, GtsTriangle *tri)
{
	GtsVertex *v1, *v2, *v3;

	gts_triangle_vertices(tri, &v1, &v2, &v3);

	if (!IS_BOK_VERTEX(v1) ||
	    !IS_BOK_VERTEX(v2) ||
	    !IS_BOK_VERTEX(v3))
		return false;

	lua_newtable(L);

	pushref(L, BOK_VERTEX(v1)->ref);
	lua_rawseti(L, -2, 1);

	pushref(L, BOK_VERTEX(v2)->ref);
	lua_rawseti(L, -2, 2);

	pushref(L, BOK_VERTEX(v3)->ref);
	lua_rawseti(L, -2, 3);

	return true;
}

static gint triangles_face_func(gpointer item, gpointer data)
{
	struct surface_foreach *fdata = (struct surface_foreach *)data;
	lua_State *L = fdata->L;
	GtsTriangle *tri = GTS_TRIANGLE(item);
	
	if (make_triangle(L, tri))
		lua_rawseti(L, fdata->setidx, ++fdata->count);

	return 0;	
}

static int mesh_triangles(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	int narg = lua_gettop(L);
	int setidx;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	lua_newtable(L);
	setidx = absidx(L, -1);

	if (narg >= 2 && lua_istable(L, 2)) {
		BokVertex *d = (BokVertex *)IS_BOK_VERTEX(getdecoration(L, 2));

		if (d != NULL) {
			GSList *list = gts_vertex_faces(GTS_VERTEX(d), mesh->surface, NULL);
			int idx = 1;
		
			for(GSList *t = list; t != NULL; t = t->next)
				if (make_triangle(L, GTS_TRIANGLE(t->data)))
					lua_rawseti(L, setidx, idx++);
		}
	} else {
		struct surface_foreach data = { L, 0, 1, setidx };

		gts_surface_foreach_face(mesh->surface, 
					 triangles_face_func, &data);
	}

	return 1;
}

struct mesh_draw_struct
{
	lua_State *L;
	float *coords;
	float *texcoords;
	GLubyte *cols;
	GLushort *prims;
	int nvert;
	int nprim;
	int ncols;
	GLfloat *deflcol;
};

static gint draw_vertex_func(gpointer item, gpointer data)
{
	struct mesh_draw_struct *mds = (struct mesh_draw_struct *)data;
	lua_State *L = mds->L;
	float x, y;
	float tx, ty;
	float col[4];

	if (!IS_BOK_VERTEX(item))
		return 1;

	BokVertex *v = BOK_VERTEX(item);

	assert(v->L == L);
	if (v->ref == LUA_NOREF)
		return 1;

	pushref(L, v->ref);

	if (!get_xy(L, -1, &x, &y)) {
		lua_pop(L, 1);
		return 1;
	}
	tx = x;
	ty = y;
	
	lua_pushliteral(L, "tx");
	lua_gettable(L, -2);
	if (lua_isnumber(L, -1))
		tx = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushliteral(L, "ty");
	lua_gettable(L, -2);
	if (lua_isnumber(L, -1))
		ty = lua_tonumber(L, -1);
	lua_pop(L, 1);

	memcpy(col, mds->deflcol, sizeof(col));
	if (get_colour(L, -1, col))
		mds->ncols++;
	lua_pop(L, 1);

	v->index = mds->nvert;
	mds->coords[mds->nvert*2+0] = x;
	mds->coords[mds->nvert*2+1] = y;
	mds->texcoords[mds->nvert*2+0] = tx;
	mds->texcoords[mds->nvert*2+1] = ty;

	mds->cols[mds->nvert*4+0] = (GLubyte)(col[0] * 255.);
	mds->cols[mds->nvert*4+1] = (GLubyte)(col[1] * 255.);
	mds->cols[mds->nvert*4+2] = (GLubyte)(col[2] * 255.);
	mds->cols[mds->nvert*4+3] = (GLubyte)(col[3] * 255.);

	mds->nvert++;

	return 1;
}

static gint draw_face_func(gpointer item, gpointer data)
{
	struct mesh_draw_struct *mds = (struct mesh_draw_struct *)data;
	GtsVertex *v1, *v2, *v3;
	GtsTriangle *tri = GTS_TRIANGLE(item);

	assert(GTS_IS_TRIANGLE(tri));

	gts_triangle_vertices(tri, &v1, &v2, &v3);
	if (!IS_BOK_VERTEX(v1) ||
	    !IS_BOK_VERTEX(v2) ||
	    !IS_BOK_VERTEX(v3))
		return 1;

	mds->prims[mds->nprim*3 + 0] = BOK_VERTEX(v1)->index;
	mds->prims[mds->nprim*3 + 1] = BOK_VERTEX(v2)->index;
	mds->prims[mds->nprim*3 + 2] = BOK_VERTEX(v3)->index;
	mds->nprim++;

	return 1;
}

static int mesh_draw(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	struct texture *tex = texture_get(L, 2);
	int nvert, nface;
	float *coords, *texcoords;
	GLushort *prims;
	GLubyte *cols;
	GLfloat col[4];

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");
	if (lua_gettop(L) >= 2)
		luaL_argcheck(L, tex != NULL, 2, "'texture' expected");

	nvert = gts_surface_vertex_number(mesh->surface);
	nface = gts_surface_face_number(mesh->surface);

	//printf("nvert=%d nface=%d\n", nvert, nface);

	coords = new float[2 * nvert];	
	texcoords = new float[2 * nvert];
	cols = new GLubyte[4 * nvert];

	prims = new GLushort[3 * nface];

	glGetFloatv(GL_CURRENT_COLOR, col);

	struct mesh_draw_struct mds = { L, coords, texcoords, cols, prims, 0, 0, 0, col };

	gts_surface_foreach_vertex(mesh->surface, draw_vertex_func, &mds);
	gts_surface_foreach_face(mesh->surface, draw_face_func, &mds);

	assert(mds.nvert <= nvert);
	assert(mds.nprim <= nface);

	render_indexed_mesh(tex, mds.nvert, coords, texcoords, mds.ncols > 0 ? cols : NULL,
			    mds.nprim, prims);

	delete[] coords;
	delete[] texcoords;
	delete[] prims;
	delete[] cols;

	return 0;
}

static int mesh_stab(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	float x, y;
	GtsPoint *pt;

	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");

	if (!get_xy(L, 2, &x, &y))
		luaL_argerror(L, 2, "'point' expected");

	pt = gts_point_new(gts_point_class(), x, y, 0);

	GtsFace *f = gts_point_locate(pt, mesh->surface, NULL);

	gts_object_destroy(GTS_OBJECT(pt));

	if (f && GTS_IS_TRIANGLE(f)) 
		make_triangle(L, GTS_TRIANGLE(f));
	else
		lua_pushnil(L);
	return 1;
}

// return a list of edges making up the mesh boundary.  This is
// defined as the edge opposite the bounding triangle vertex in
// triangles with one bounding triangle vertex.
static int mesh_boundary(lua_State *L)
{
	struct mesh *mesh = mesh_get(L, 1);
	luaL_argcheck(L, mesh != NULL, 1, "'mesh' expected");
	GSList *vertices = NULL;

	lua_newtable(L);
	
	vertices = g_slist_prepend(vertices, mesh->boundary[0]);
	vertices = g_slist_prepend(vertices, mesh->boundary[1]);
	vertices = g_slist_prepend(vertices, mesh->boundary[2]);

	GSList *edges = gts_edges_from_vertices(vertices, mesh->surface);

	GSList *triangles = gts_triangles_from_edges(edges);

	for(GSList *t = triangles; t != NULL; t = t->next) {
		GtsEdge *e[3];
		int count = 0, idx = -1;

		for(int i = 0; i < 3; i++) {
			e[i] = gts_triangle_edge_opposite(GTS_TRIANGLE(t->data), mesh->boundary[i]);
			if (e[i] != NULL) {
				count++;
				idx = i;
			}
		}

		// there should only be one edge opposte a boundary
		// vertex; if there is more than 1, then it means this
		// triangle has a n edge at the boundary.
		if (count != 1 || !IS_BOK_EDGE(e[idx]))
			continue;
		
		store_lua_edge(L, 1, -1, BOK_EDGE(e[idx]));
	}

	g_slist_free(vertices);
	g_slist_free(edges);
	g_slist_free(triangles);

	return 1;
}

// mesh userdata meta
static const luaL_reg meshuser_meta[] = {
	{ "__gc",	mesh_gc },
};

// mesh methods
static const luaL_reg mesh_meta[] = {
	{ "add",	mesh_add },
	{ "del",	mesh_del },
	{ "move",	mesh_move },

	{ "points",	mesh_points },
	{ "edges",	mesh_edges },
	{ "triangles",	mesh_triangles },
	{ "connected",	mesh_connected },

	{ "boundary",	mesh_boundary },

	{ "stab",	mesh_stab },

	{ "draw",	mesh_draw },

	{0,0}
};

static const luaL_reg mesh_library[] = {
	{ "new",	mesh_new },

	{0,0}
};

void mesh_register(lua_State *L)
{
	// mesh library
	luaL_openlib(L, "mesh", mesh_library, 0);
	lua_pop(L, 1);

	// mesh instance methods
	luaL_newmetatable(L, "bokchoi.mesh");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);  /* pushes the metatable */
	lua_settable(L, -3);  /* metatable.__index = metatable */
  
	luaL_openlib(L, 0, mesh_meta, 0);
	lua_pop(L, 1);

	// mesh userdata
	luaL_newmetatable(L, "bokchoi.meshuserdata");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);  /* pushes the metatable */
	lua_settable(L, -3);  /* metatable.__index = metatable */
  
	luaL_openlib(L, 0, meshuser_meta, 0);

	lua_pop(L, 2);
}
