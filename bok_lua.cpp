// Lua interfaces
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <klt.h>
}

static lua_State *state;

/* ----------------------------------------------------------------------
   Tracker interface
   ---------------------------------------------------------------------- */

// userdata tracker structure
struct tracker
{
	KLT_TrackingContextRec *tc;
	KLT_FeatureListRec *fl;

	int min, max;
	int active;
};

extern unsigned char *img;
extern unsigned int img_w, img_h;

// Update feature set with tracking results
// Args: tracker feature_set
static int tracker_track(lua_State *L)
{
	int narg = lua_gettop(L);
	struct tracker *tc;
	int active;

	if (narg != 2 || !lua_isuserdata(L, 1) || !lua_istable(L, 2)) {
		lua_pushstring(L, "args: tracker features");
		lua_error(L);
	}

	if (img == NULL) {
		lua_pushstring(L, "image not read yet");
		lua_error(L);
	}

	tc = (struct tracker *)lua_touserdata(L, 1);

	if (tc->active == 0)
		KLTSelectGoodFeatures(tc->tc, img, img_w, img_h, tc->fl);
	if (tc->active < tc->min)
		KLTReplaceLostFeatures(tc->tc, img, img_w, img_h, tc->fl);
	else
		KLTTrackFeatures(tc->tc, img, img, img_w, img_h, tc->fl);

	active = 0;

	for(int i = 0; i < tc->fl->nFeatures; i++) {
		KLT_Feature f = tc->fl->feature[i];
		int lidx = i+1;	// lua idx - 1-based arrays

		lua_pushnumber(L, lidx);
		lua_gettable(L, 2);

		// stack: tracker features point

		// compare the state of the point in the KLT features
		// array with the state in the feature_set table, and
		// update accordingly.
		if ((f->val < 0) && lua_isnil(L, -1)) {
			// stk: tracker features 

			// nothing to do
		} else if ((f->val < 0) && lua_istable(L, -1)) {
			// lost feature
			static const char *reasons[] = {
				"not_found",
				"small_det",
				"max_iter",
				"oob",
				"large_residue"
			};
			int ridx = -f->val - 1;

			// see if there's a lost method in top
			lua_pushstring(L, "lost");
			lua_gettable(L, -2);
			if (lua_isfunction(L, -1)) {
				// stk: tracker features point func
				lua_pushvalue(L, -2);
				if (ridx >= 0 && ridx < 5)
					lua_pushstring(L, reasons[ridx]);
				else
					lua_pushnil(L);
				lua_call(L, 2, 0);
			} else
				lua_pop(L, 1);

			// stk: tracker features 
			lua_pushnumber(L, lidx);
			lua_pushnil(L);
			lua_settable(L, 2);
			
			// stk: tracker features
		} else if ((f->val >= 0) && lua_isnil(L, -1)) {
			// new feature
			// look for "add" method in features
			lua_pushstring(L, "add");
			lua_gettable(L, 2);
			if (lua_isfunction(L, -1)) {
				lua_pushvalue(L, 2); // features
				lua_pushnumber(L, lidx); // idx
				lua_pushnumber(L, f->x);
				lua_pushnumber(L, f->y);
				lua_pushnumber(L, f->val);
				lua_call(L, 5, 0);
			} else {
				lua_pushnumber(L, lidx);
				lua_newtable(L);

				lua_pushstring(L, "x");
				lua_pushnumber(L, f->x);
				lua_settable(L, -3);

				lua_pushstring(L, "y");
				lua_pushnumber(L, f->y);
				lua_settable(L, -3);

				lua_pushstring(L, "weight");
				lua_pushnumber(L, f->val);
				lua_settable(L, -3);

				lua_settable(L, 2);
			}

			active++;
		} else if ((f->val >= 0) && lua_istable(L, -1)) {
			// update feature
			
			// look for "move" in point
			lua_pushstring(L, "move");
			lua_gettable(L, 3);
			if (lua_isfunction(L, -1)) {
				lua_pushvalue(L, -2); // point
				lua_pushnumber(L, f->x);
				lua_pushnumber(L, f->y);
				lua_call(L, 3, 0);
			} else {
				// manual update
				lua_pushstring(L, "x");
				lua_pushnumber(L, f->x);
				lua_settable(L, 3);

				lua_pushstring(L, "y");
				lua_pushnumber(L, f->y);
				lua_settable(L, 3);
			}
			active++;
		}
		
		// drop everything except tracker and features
		lua_settop(L, 2);
	}

	tc->active = active;

	return 0;
}

static int tracker_index(lua_State *L)
{
	struct tracker *tc;
	const char *str;

	if (!lua_isuserdata(L, 1)) {
		lua_pushstring(L, "tracker:index not passed tracker");
		lua_error(L);
	}

	if (!lua_isstring(L, 2)) {
		lua_pushstring(L, "tracker index must be string");
		lua_error(L);
	}

	tc = (struct tracker *)lua_touserdata(L, 1);
	str = lua_tostring(L, 2);

	if (strcmp(str, "track") == 0)
		lua_pushcfunction(L, tracker_track);
	else if (strcmp(str, "active") == 0)
		lua_pushnumber(L, tc->active);
	else if (strcmp(str, "min") == 0)
		lua_pushnumber(L, tc->min);
	else if (strcmp(str, "max") == 0)
		lua_pushnumber(L, tc->max);
	else
		lua_pushnil(L);

	return 1;
}

// Creates a tracker userdata type
// args (min, max, [ mindist ])
static int new_tracker(lua_State *L)
{
	struct tracker *tc;
	int narg = lua_gettop(L);
	int min, max;
	int mindist = 15;

	if (narg < 2 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
		lua_pushstring(L, "need min and max");
		lua_error(L);
	}

	min = (int)lua_tonumber(L, 1);
	max = (int)lua_tonumber(L, 2);

	if (narg >= 3 && lua_isnumber(L, 3))
		mindist = (int)lua_tonumber(L, 3);

	tc = (struct tracker *)lua_newuserdata(L, sizeof(*tc));	// user
	tc->tc = KLTCreateTrackingContext();
	KLTSetVerbosity(0);

	tc->tc->sequentialMode = true;
	tc->tc->mindist = mindist;
	tc->fl = KLTCreateFeatureList(max);

	tc->min = min;
	tc->max = max;
	tc->active = 0;

	lua_pushstring(L, "_meta_tracker");	// user name
	lua_gettable(L, LUA_REGISTRYINDEX);	// user meta
	if (!lua_istable(L, -1)) {
		lua_pushstring(L, "missing _meta_tracker in registry");
		lua_error(L);
	} 
	lua_setmetatable(L, -2);
	
	return 1;		// return user
}

static int tracker_gc(lua_State *L)
{
	struct tracker *tc;

	if (!lua_isuserdata(L, 1)) {
		lua_pushstring(L, "tracker_gc: not userdata");
		lua_error(L);
	}

	tc = (struct tracker *)lua_touserdata(L, 1);
	KLTFreeFeatureList(tc->fl);
	KLTFreeTrackingContext(tc->tc);

	return 0;
}

// tracker meta has the following fields:
// - __gc - garbage collection
// - track(self, features) - update feature set
static void init_tracker_meta(lua_State *L)
{
	lua_newtable(L);			// T

	lua_pushstring(L, "__gc");		// T str
	lua_pushcfunction(L, tracker_gc);	// T str func
	lua_settable(L, 1);			// T

	lua_pushstring(L, "__index");		// T str
	lua_pushcfunction(L, tracker_index);	// T str func
	lua_settable(L, 1);			// T

	// register metatable
	lua_pushstring(L, "_meta_tracker");	// T str
	lua_pushvalue(L, 1);			// T str T
	lua_settable(L, LUA_REGISTRYINDEX);	// T

	lua_pop(L, 1);
}

/* ----------------------------------------------------------------------
   Graphics interface
   ---------------------------------------------------------------------- */
#include <GL/gl.h>

// userdata texture structure
struct texture {
	GLuint texid;
	int width;
	int height;

	enum format {
		mono,
		monoa,
		rgb,
		rgba
	};
};

static int texture_gc(lua_State *L)
{
}

static int texture_index(lua_State *L)
{
}

static void init_texture_meta(lua_State *L)
{
	lua_newtable(L);

	lua_pushstring(L, "__gc");
	lua_pushcfunction(L, texture_gc);
	lua_settable(L, 1);

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, texture_index);
	lua_settable(L, 1);

	lua_pushstring(L, "_meta_texture");
	lua_pushvalue(L, 1);
	lua_settable(L, LUA_REGISTRYINDEX);

	lua_pop(L, 1);
}

static int new_texture(lua_State *L)
{
	
}

// Create a global table called "gfx", which wraps up the graphics
// related functions and objects.
static void init_graphics(lua_State *L)
{
	init_texture_meta(L);

	lua_pushstring(L, "gfx");

	lua_newtable(L);

	lua_pushstring(L, "texture");
	lua_pushcfunction(L, new_texture);
	lua_settable(L, 2);

	lua_settable(L, LUA_GLOBALINDEX);
}

/* ----------------------------------------------------------------------
   Lua setup
   ---------------------------------------------------------------------- */
static int panic(lua_State *L)
{
	fprintf(stderr, "LUA panic: %s\n", lua_tostring(L, -1));

	return 0;
}

void lua_setup(const char *src)
{
	int ret;
	lua_State *L;

	L = state = lua_open();

	lua_atpanic(L, panic);

	luaopen_base(L);
	luaopen_table(L);
	luaopen_io(L);
	luaopen_string(L);
	luaopen_math(L);
	
	init_tracker_meta(state);

	lua_register(L, "new_tracker", new_tracker);

	ret = luaL_loadfile(L, src);
	if (ret) {
		switch(ret) {
		case LUA_ERRFILE:
			fprintf(stderr, "file error loading %s\n", src);
			break;
		case LUA_ERRSYNTAX:
			fprintf(stderr, "syntax error loading %s\n", src);
			break;
			
		default:
			fprintf(stderr, "error loading %s\n", src);
			break;
		}
		exit(1);
	}
	if (lua_isfunction(L, -1))
		lua_call(L, 0, 0);
}

void lua_cleanup()
{
	lua_close(state);
}

void lua_frame()
{
	// call process_frame, if any
	lua_pushstring(state, "process_frame");
	lua_gettable(state, LUA_GLOBALSINDEX);
	if (lua_isfunction(state, -1))
		lua_call(state, 0, 0);
	else
		lua_pop(state, 1);
}
