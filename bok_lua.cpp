// Lua interfaces
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <klt.h>
}

#include <png.h>
#include <GL/glu.h>

static lua_State *state;

#define GLERR() _glerr(__FILE__, __LINE__);

static void _glerr(const char *file, int line)
{
	GLenum err = glGetError();

	if (err != GL_NO_ERROR) {
		printf("GL error at %s:%d: %s\n",
		       file, line, (char *)gluErrorString(err));
	}
}

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
static int tracker_new(lua_State *L)
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

	luaL_getmetatable(L, "tracker");	// user meta
	if (!lua_istable(L, -1)) {
		lua_pushstring(L, "missing tracker.__meta in registry");
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

// Tracker library
static const luaL_reg tracker_methods[] = {
	{ "new",   tracker_new },

	{ 0,0 }
};

// Tracker object metatable
static const luaL_reg tracker_meta[] = {
	{ "__gc",	tracker_gc },
	{ "__index",	tracker_index },

	{ 0,0 }
};

static int tracker_register(lua_State *L)
{
	luaL_openlib(L, "tracker", tracker_methods, 0);	// lib

	luaL_newmetatable(L, "tracker");		// lib meta

	luaL_openlib(L, 0, tracker_meta, 0);		// lib meta

	lua_pop(L, 2);
	return 1;
}

/* ----------------------------------------------------------------------
   Graphics interface
   ---------------------------------------------------------------------- */

// userdata texture structure
struct texture {
	GLuint texid;
	int width;
	int height;
	int texwidth, texheight;

	float sw, sh;		// scale

	GLenum format;
};

static int texture_index(lua_State *L)
{
	struct texture *tex;
	const char *str;

	if (!lua_isuserdata(L, 1)) {
		lua_pushstring(L, "tracker:index not passed tracker");
		lua_error(L);
	}

	if (!lua_isstring(L, 2)) {
		lua_pushstring(L, "tracker index must be string");
		lua_error(L);
	}

	tex = (struct texture *)lua_touserdata(L, 1);
	str = lua_tostring(L, 2);

	if (strcmp(str, "width") == 0)
		lua_pushnumber(L, tex->width);
	else if (strcmp(str, "height") == 0)
		lua_pushnumber(L, tex->height);
	else if (strcmp(str, "format") == 0) {
		const char *fmt = "?";

		switch(tex->format) {
		case GL_LUMINANCE:		fmt = "mono"; break;
		case GL_LUMINANCE_ALPHA:	fmt = "monoa"; break;
		case GL_RGB:			fmt = "rgb"; break;
		case GL_RGBA:			fmt = "rgba"; break;
		}
		lua_pushstring(L, fmt);
	} else
		lua_pushnil(L);

	return 1;
}

static unsigned power2(unsigned x)
{
	unsigned ret = 1;

	while(ret < x)
		ret <<= 1;

	return ret;
}

// Create a texture userdata object.
// Args: filename
static int texture_new(lua_State *L)
{
	const char *filename;
	FILE *fp;
	struct texture *tex = NULL;
	int channels;
	GLenum fmt;
	int texwidth, texheight;

	const char *error = NULL;
	png_structp png_ptr = NULL;
	png_infop info_ptr = NULL;
	png_infop end_info = NULL;

	if (!lua_isstring(L, 1)) {
		lua_pushstring(L, "need filename");
		lua_error(L);
	}

	filename = lua_tostring(L, 1);
	fp = fopen(filename, "rb");

	if (fp == NULL) {
		lua_pushfstring(L, "Can't open PNG file %s: %s",
				filename, strerror(errno));
		lua_error(L);
	}

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 
					 NULL, NULL, NULL);

	if (!png_ptr) {
		error = "failed to allocate png_struct";
		goto out;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		error = "failed to allocate png_info";
		goto out;
	}

	end_info = png_create_info_struct(png_ptr);
	if (!end_info)
	{
		error = "failed to allocate end png_info";
		goto out;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		error = "PNG error";
		goto out;
	}
	
	png_init_io(png_ptr, fp);

	png_read_png(png_ptr, info_ptr, 
		     PNG_TRANSFORM_STRIP_16 |
		     PNG_TRANSFORM_PACKING |
		     PNG_TRANSFORM_EXPAND, NULL);

	png_uint_32 width, height;
	int bitdepth, color_type;

	png_get_IHDR(png_ptr, info_ptr, &width, &height,
		     &bitdepth, &color_type, NULL, NULL, NULL);

	printf("width=%d height=%d bitdepth=%d color_type=%d\n",
	       width, height, bitdepth, color_type);

	switch(color_type) {
	case PNG_COLOR_TYPE_GRAY:
		fmt = GL_LUMINANCE;
		channels = 1;
		break;

	case PNG_COLOR_TYPE_GRAY_ALPHA:
		fmt = GL_LUMINANCE_ALPHA;
		channels = 2;
		break;

	case PNG_COLOR_TYPE_RGB:
		fmt = GL_RGB;
		channels = 3;
		break;

	case PNG_COLOR_TYPE_RGB_ALPHA:
		fmt = GL_RGBA;
		channels = 4;
		break;

	default:
		error = "unsupported image format";
		goto out;
	}

	texwidth = power2(width);
	texheight = power2(height);

	{
		png_byte pixels[texwidth * texheight * channels];
		png_bytep *rows;

		memset(pixels, 0, texwidth * texheight * channels);

		rows = png_get_rows(png_ptr, info_ptr);
		int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

		for(unsigned r = 0; r < height; r++) {
			png_byte *row = &pixels[r * texwidth * channels];
			memcpy(row, rows[r], rowbytes);
		}

		tex = (struct texture *)lua_newuserdata(L, sizeof(*tex));

		glGenTextures(1, &tex->texid);
		tex->width = width;
		tex->height = height;
		tex->format = fmt;
		tex->texwidth = texwidth;
		tex->texheight = texheight;

		tex->sw = (float)width / texwidth;
		tex->sh = (float)height / texheight;

		luaL_getmetatable(L, "texture");
		lua_setmetatable(L, -2);

		glBindTexture(GL_TEXTURE_2D, tex->texid);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		gluBuild2DMipmaps(GL_TEXTURE_2D, fmt, 
				  texwidth, texheight, 
				  fmt, GL_UNSIGNED_BYTE, pixels);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
				GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
				GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
				GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_LINEAR_MIPMAP_LINEAR);
		GLERR();
	}

  out:
	fclose(fp);

	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);

	if (error != NULL) {
		lua_pushstring(L, error);
		lua_error(L);
	}

	return 1;
}

static int texture_gc(lua_State *L)
{
	struct texture *tex;

	if (!lua_isuserdata(L, 1)) {
		lua_pushstring(L, "texture_gc: not userdata");
		lua_error(L);
	}

	tex = (struct texture *)lua_touserdata(L, 1);

	glDeleteTextures(1, &tex->texid);

	return 0;
}

static const luaL_reg texture_meta[] = {
	{ "__gc",	texture_gc },
	{ "__index",	texture_index },

	{0,0}
};

static int gfx_point(lua_State *L)
{
	int narg = lua_gettop(L);
	float x, y;

	if (narg < 2) {
		lua_pushstring(L, "need at least (x,y)");
		lua_error(L);
	}
	x = lua_tonumber(L, 1);
	y = lua_tonumber(L, 2);

	glBegin(GL_POINTS);
	glVertex2f(x, y);
	glEnd();

	return 0;
}

// args: texture x y scale
static int gfx_sprite(lua_State *L)
{
	struct texture *tex;
	float x, y, scale;
	float dx, dy;

	if (!lua_isuserdata(L, 1)) {
		lua_pushstring(L, "need texture");
		lua_error(L);
	}

	tex = (struct texture *)lua_touserdata(L, 1);

	x = lua_tonumber(L, 2);
	y = lua_tonumber(L, 3);
	scale = lua_tonumber(L, 4);

	dx = tex->width * scale / 2;
	dy = tex->height * scale / 2;

	glBindTexture(GL_TEXTURE_2D, tex->texid);
	glEnable(GL_TEXTURE_2D);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);

	if (0)
		printf("id=%d x=%g,y=%g dx=%g dy=%g sw=%g sh=%g\n",
		       tex->texid, x, y, dx, dy, tex->sw, tex->sh);
	
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0);
	glVertex2f(x - dx, y - dy);

	glTexCoord2f(tex->sw, 0);
	glVertex2f(x + dx, y - dy);

	glTexCoord2f(tex->sw, tex->sh);
	glVertex2f(x + dx, y + dy);

	glTexCoord2f(0, tex->sh);
	glVertex2f(x - dx, y + dy);
	glEnd();

	GLERR();

	glDisable(GL_TEXTURE_2D);
	return 0;
}

// Expect to see a table on the stack at idx;
// look for interesting elements
static void setstate(lua_State *L, int idx)
{
	int top = lua_gettop(L);

	if (idx < 0)
		idx = top + idx + 1;

	if (lua_gettop(L) < 1 || !lua_istable(L, idx)) {
		lua_pushstring(L, "state is not a table");
		lua_error(L);
	}

	lua_pushstring(L, "colour");
	lua_gettable(L, idx);
	
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_pushstring(L, "color");
		lua_gettable(L, idx);
	}

	if (lua_istable(L, -1)) {
		// Colour is either { R, G, B, A } or { r=R, g=G, b=B, a=A }
		// (or a mixture)
		static const char *channels[] = { "r", "g", "b", "a" };
		float col[4] = { 1, 1, 1, 1 };

		for(int i = 0; i < 4; i++) {
			lua_pushstring(L, channels[i]);
			lua_gettable(L, -2);

			if (!lua_isnumber(L, -1)) {
				lua_pop(L, 1);
				lua_pushnumber(L, i+1);
				lua_gettable(L, -2);

				if (!lua_isnumber(L, -1)) {
					lua_pop(L, 1);
					break;
				}
			}
			col[i] = lua_tonumber(L, -1);
			lua_pop(L, 1);
		}

		glColor4fv(col);
	}
	lua_settop(L, top);

	lua_pushstring(L, "blend");
	lua_gettable(L, idx);
	if (lua_isstring(L, -1)) {
		const char *str = lua_tostring(L, -1);

		if (strcmp(str, "none") == 0)
			glDisable(GL_BLEND);
		else if (strcmp(str, "add") == 0) {
			glBlendFunc(GL_ONE, GL_ONE);
			glEnable(GL_BLEND);
		} else if (strcmp(str, "1-alpha") == 0) {
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_BLEND);
		} else {
			lua_pushstring(L, "bad blend mode");
			lua_error(L);
		}
	}
	lua_settop(L, top);

	if (lua_isnumber(L, -1))
		glPointSize(lua_tonumber(L, -1));
	lua_settop(L, top);
}

static int gfx_setstate(lua_State *L)
{
	setstate(L, -1);

	return 0;
}


static const luaL_reg gfx_methods[] = {
	// rendering operations
	{ "point",	gfx_point },
	{ "sprite",	gfx_sprite },

	// state setting
	{ "setstate",	gfx_setstate },
//	{ "getstate",	gfx_getstate },

	// texture constructor
	{ "texture",	texture_new },

	{0,0}
};

static void gfx_register(lua_State *L)
{
	luaL_openlib(L, "gfx", gfx_methods, 0);

	luaL_newmetatable(L, "texture");
	luaL_openlib(L, NULL, texture_meta, 0);
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

	tracker_register(state);
	gfx_register(state);

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
