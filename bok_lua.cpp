// Lua interfaces
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <klt.h>
}

#include <png.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glu.h>
#include <GL/glext.h>

#include "bok_lua.h"
#include "bok_mesh.h"
#include "bok_text.h"

#ifndef GL_TEXTURE_RECTANGLE
#if GL_EXT_texture_rectangle
#define GL_TEXTURE_RECTANGLE GL_TEXTURE_RECTANGLE_EXT
#elif GL_ARB_texture_rectangle
#define GL_TEXTURE_RECTANGLE GL_TEXTURE_RECTANGLE_ARB
#else
#define GL_TEXTURE_RECTANGLE 0
#endif
#endif

#ifndef GL_GENERATE_MIPMAP
#ifdef GL_SGIS_generate_mipmap
#define GL_GENERATE_MIPMAP GL_GENERATE_MIPMAP_SGIS
#else
#define GL_GENERATE_MIPMAP 0
#endif
#endif	// GL_GENERATE_MIPMAP

static lua_State *state;

void _glerr(const char *file, int line)
{
	GLenum err = glGetError();

	fflush(stderr);
	if (err != GL_NO_ERROR) {
		printf("GL error at %s:%d: %s\n",
		       file, line, (char *)gluErrorString(err));
	}
}

static unsigned char *img;
static unsigned img_w, img_h;

static bool ext_texture_rect;
static int  max_texture_units;
static bool ext_generate_mipmap;

/* ----------------------------------------------------------------------
   Useful
   ---------------------------------------------------------------------- */
// convert a relative stack reference to absolute
int absidx(lua_State *L, int idx)
{
	if (idx < 0 && idx > LUA_REGISTRYINDEX)
		idx = lua_gettop(L) + idx + 1;

	return idx;
}

bool get_xy(lua_State *L, int idx, float *x, float *y)
{
	idx = absidx(L, idx);

	if (!lua_istable(L, idx)) {
		*x = *y = 0;
		return false;
	}

	lua_pushliteral(L, "x");
	lua_gettable(L, idx);
	*x = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushliteral(L, "y");
	lua_gettable(L, idx);
	*y = lua_tonumber(L, -1);
	lua_pop(L, 1);

	return true;
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

static int tracker_gc(lua_State *L);

static struct tracker *tracker_get(lua_State *L, int idx)
{
	struct tracker *tracker;

	tracker = (struct tracker *)luaL_checkudata(L, idx, "bokchoi.tracker");
	return tracker;
}

// Update feature set with tracking results
// Args: tracker feature_set
static int tracker_track(lua_State *L)
{
	int narg = lua_gettop(L);
	struct tracker *tc;
	int active;

	if (narg != 2 || !lua_isuserdata(L, 1) || !lua_istable(L, 2))
		luaL_error(L, "args: tracker features");

	if (img == NULL)
		luaL_error(L, "image not read yet");

	tc = tracker_get(L, 1);

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
			if (!call_lua(L, 0, 2, "add", "Iiffi", 2, lidx, f->x, f->y, f->val)) {
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
			if (!call_lua(L, 0, 3, "move", "Iff", -1, f->x, f->y)) {
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

	tc = tracker_get(L, 1);

	if (!lua_isstring(L, 2))
		luaL_error(L, "tracker index must be string");

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

	if (narg < 2 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		luaL_error(L, "need min and max");

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

	luaL_getmetatable(L, "bokchoi.tracker");	// user meta
	if (!lua_istable(L, -1))
		luaL_error(L, "missing tracker.__meta in registry");

	lua_setmetatable(L, -2);
	
	return 1;		// return user
}

static int tracker_gc(lua_State *L)
{
	struct tracker *tc;

	tc = tracker_get(L, 1);

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

	luaL_newmetatable(L, "bokchoi.tracker");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);  /* pushes the metatable */

	luaL_openlib(L, 0, tracker_meta, 0);

	lua_pop(L, 2);
	return 1;
}

/* ----------------------------------------------------------------------
   Graphics interface
   ---------------------------------------------------------------------- */

static struct texture_list *pending_delete;
struct texture_list
{
	struct texture_list *next;
	GLuint texid;

	texture_list(GLuint id)
		:texid(id)
		{}

	void addlist(struct texture_list **list) {
		next = *list;
		*list = this;
	}
};

static void add_pending_delete(GLuint texid)
{
	struct texture_list *e = new texture_list(texid);
	e->addlist(&pending_delete);
}

static void delete_pending(void)
{
	for (texture_list *p = pending_delete, *n; p != NULL; p = n) {
		n = p->next;
		glDeleteTextures(1, &p->texid);
		delete p;
	}

	pending_delete = NULL;
}

struct texture *texture_init(struct texture *tex,
			     GLenum target, int width, int height, GLenum format)
{
	tex->width = width;
	tex->height = height;
	tex->format = format;
	tex->target = target;

	return tex;
}

struct texture *texture_alloc(lua_State *L, GLenum target,
			      int width, int height, GLenum format)
{
	struct texture *tex = (struct texture *)lua_newuserdata(L, sizeof(*tex));

	if (tex == NULL)
		luaL_error(L, "can't allocate texture");

	texture_init(tex, target, width, height, format);

	return tex;
}

struct texture *texture_get(lua_State *L, int idx)
{
	struct texture *ret;

	ret = (struct texture *)luaL_checkudata(L, idx, "bokchoi.texture");

	if (ret == NULL)
		ret = (struct texture *)luaL_checkudata(L, idx, "bokchoi.glyph");
	if (ret == NULL)
		luaL_argcheck(L, ret != NULL, idx, "need 'texture' or 'glyph'");

	return ret;
}

int texture_index(lua_State *L)
{
	struct texture *tex;
	const char *str;

	tex = texture_get(L, 1);

	if (!lua_isstring(L, 2))
		luaL_error(L, "texture index must be string");

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

void texture_init_texcoord(struct texture *tex, float w, float h)
{
	tex->tc[0*2+0] = 0;
	tex->tc[0*2+1] = 0;

	tex->tc[1*2+0] = w;
	tex->tc[1*2+1] = 0;

	tex->tc[2*2+0] = w;
	tex->tc[2*2+1] = h;

	tex->tc[3*2+0] = 0;
	tex->tc[3*2+1] = h;
}

// Make a texture from a frame of input.  This uses the texture_rect
// extension if possible, to avoid having to allocate lots of texture
// memory and handle the non-power-of-2 edge cases.
//
// TODO: defer allocating texture memory until we first render?
static int texture_new_frame(lua_State *L,
			     const unsigned char *img, 
			     int width, int height,
			     GLenum fmt)
{
	struct texture *tex;

	GLERR();

	tex = texture_alloc(L, GL_TEXTURE_2D, width, height, fmt);

	glGenTextures(1, &tex->texid);

	luaL_getmetatable(L, "bokchoi.texture");
	lua_setmetatable(L, -2);

	if (ext_texture_rect && GL_TEXTURE_RECTANGLE) {
		tex->texwidth = width;
		tex->texheight = height;

		// scale factor for 0..1 texture coords
		// texture_rect textures have non-parametric coords
		texture_init_texcoord(tex, width, height);

		tex->target = GL_TEXTURE_RECTANGLE;
	} else {
		// XXX FIXME
		tex->texwidth = power2(width);
		tex->texheight = power2(height);

		texture_init_texcoord(tex, 
				      (float)width / tex->texwidth,
				      (float)height / tex->texheight);

		tex->target = GL_TEXTURE_2D;
	}

	glBindTexture(tex->target, tex->texid);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexImage2D(tex->target, 0, fmt, 
		     tex->texwidth, tex->texheight, 0,
		     fmt, GL_UNSIGNED_BYTE, img);
	
	glTexParameteri(tex->target, GL_TEXTURE_WRAP_S,
			GL_CLAMP_TO_EDGE);
	glTexParameteri(tex->target, GL_TEXTURE_WRAP_T,
			GL_CLAMP_TO_EDGE);
	glTexParameteri(tex->target, GL_TEXTURE_MAG_FILTER,
			GL_LINEAR);
	glTexParameteri(tex->target, GL_TEXTURE_MIN_FILTER,
			GL_LINEAR);

	GLERR();

	return 1;
}

// Create a texture userdata object.
// Args: filename
static int texture_new_png(lua_State *L)
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

	if (!lua_isstring(L, 1))
		luaL_error(L, "need filename");

	filename = lua_tostring(L, 1);
	fp = fopen(filename, "rb");

	if (fp == NULL)
		luaL_error(L, "Can't open PNG file %s: %s",
			   filename, strerror(errno));

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
	if (!end_info) {
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

	printf("width=%u height=%u bitdepth=%d color_type=%d\n",
	       (unsigned)width, (unsigned)height, bitdepth, color_type);

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

		tex = texture_alloc(L, GL_TEXTURE_2D, width, height, fmt);

		glGenTextures(1, &tex->texid);
		tex->texwidth = texwidth;
		tex->texheight = texheight;

		texture_init_texcoord(tex, 
				      (float)width / tex->texwidth,
				      (float)height / tex->texheight);
		
		luaL_getmetatable(L, "bokchoi.texture");
		lua_setmetatable(L, -2);

		glBindTexture(GL_TEXTURE_2D, tex->texid);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (ext_generate_mipmap) {
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP,
					GL_TRUE);
			glTexImage2D(GL_TEXTURE_2D, 0, fmt, 
				     texwidth, texheight, 0,
				     fmt, GL_UNSIGNED_BYTE, pixels);
			GLERR();
		} else
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

	if (error != NULL)
		luaL_error(L, error);

	return 1;
}

int texture_gc(lua_State *L)
{
	struct texture *tex;

	if (!lua_isuserdata(L, 1))
		luaL_error(L, "texture_gc: not userdata");

	tex = texture_get(L, 1);

	add_pending_delete(tex->texid);

	return 0;
}

static const luaL_reg texture_meta[] = {
	{ "__gc",	texture_gc },
	{ "__index",	texture_index },

	{0,0}
};

static int gfx_line(lua_State *L)
{
	int narg = lua_gettop(L);

	if (narg < 2)
		return 0;

	glBegin(GL_LINE_STRIP);

	for(int pt = 1; pt <= narg; pt++) {
		float x, y;

		if (!get_xy(L, pt, &x, &y))
			continue;
		glVertex2f(x, y);
	}

	glEnd();
	GLERR();

	return 0;
}

static int gfx_point(lua_State *L)
{
	int narg = lua_gettop(L);

	if (narg < 1)
		luaL_error(L, "need at least one point");

	glBegin(GL_POINTS);

	for(int pt = 1; pt <= narg; pt++) {
		float x, y;

		if (!get_xy(L, pt, &x, &y))
			continue;

		glVertex2f(x, y);
	}

	glEnd();

	return 0;
}

// args: pt nil|size|{width,height} texture [texture...]
//
// Where size is the size of the longest edge; a nil or zero size
// displays the full-sized texture.  The first texture is the base
// texture, which is used for the size/position calculations.  The
// other textures are multiplied in using multitexturing (to the
// extent the hardware supports it).
//
// pt is taken to be the centre of the the texture, so the sides are
// at (pt.x-width/2, pt.x+width/2, pt.y-height/2, pt.y+height/2).  If
// a transform is current, then these corner vertices are transformed.
static int gfx_sprite(lua_State *L)
{
	float x, y, width, height;
	int narg = lua_gettop(L);
	int ntex = 0;

	if (narg < 3)
		luaL_error(L, "sprite(pt, nil|size|{width, height}, texture, [texture...])");

	if (!get_xy(L, 1, &x, &y))
		luaL_argerror(L, 1, "'point' expected");

	for(int i = 3; i <= narg; i++)
		if (texture_get(L, i))
			ntex++;

	if (ntex == 0)
		luaL_error(L, "need at least one texture");

	if (ntex > max_texture_units)
		ntex = max_texture_units;
	
	struct texture *t[ntex];

	{
		int idx = 0;
		for(int i = 3; i <= narg; i++)
			if (lua_isuserdata(L, i))
				t[idx++] = texture_get(L, i);
		assert(idx == ntex);
	}

	if (t[0]->width == 0 || t[0]->height == 0)
		luaL_error(L, "bad %dx%d texture", 
			   t[0]->width, t[0]->height);

	width = t[0]->width;
	height = t[0]->height;

	// Get either a size number or a {width,height} table
	if (lua_isnumber(L, 2)) {
		float size = lua_tonumber(L, 2);

		if (t[0]->width > t[0]->height) {
			width = size;
			height = (t[0]->height * size) / t[0]->width;
		} else {
			height = size;
			width = (t[0]->width * size) / t[0]->height;
		}
	} else if (lua_istable(L, 2)) {
		lua_pushnumber(L, 1);
		lua_gettable(L, 2);
		width = lua_tonumber(L, -1);

		lua_pushnumber(L, 2);
		lua_gettable(L, 2);
		height = lua_tonumber(L, -1);
		
		lua_pop(L, 2);
	}

	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

	glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
	glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

	for(int i = 0; i < ntex; i++) {
		glClientActiveTexture(GL_TEXTURE0 + i);
		glActiveTexture(GL_TEXTURE0 + i);

		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);

		glBindTexture(t[i]->target, t[i]->texid);
		glEnable(t[i]->target);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, &t[i]->tc[0]);

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		if (0)
			printf("%d: id=%d x=%g,y=%g sw=%d sh=%d\n",
			       i, t[i]->texid, x, y, 
			       t[i]->width, t[i]->height);
	}

	static const float v[2*4] = { -.5, -.5,
				       .5, -.5,
				       .5,  .5,
				      -.5,  .5 };

	glPushMatrix();
	glTranslatef(x, y, 0);
	glScalef(width, height, 1);

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, v);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glPopMatrix();
	glPopClientAttrib();
	glPopAttrib();

	GLERR();

	return 0;
}

void render_indexed_mesh(struct texture *tex, 
			 int nvert, 
			 float *coords, float *texcoords, GLubyte *cols,
			 int nprims, GLushort *prims)
{
	// coords are screen coords
	// texcoords are in the original texture coordinate space
	// no texture if tex is NULL or texcoords==NULL

	if (tex == NULL || texcoords == NULL) {
		tex = NULL;
		texcoords = NULL;
	}

	if (coords == NULL || prims == NULL || nvert == 0 || nprims == 0)
		return;

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, coords);
	GLERR();

	if (tex != NULL) {
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
		GLERR();

		glEnable(tex->target);
		glBindTexture(tex->target, tex->texid);

		if (tex->target != GL_TEXTURE_RECTANGLE) {
			glMatrixMode(GL_TEXTURE);
			glLoadIdentity();
			glScalef(1./tex->width, 1./tex->height, 1);
			glMatrixMode(GL_MODELVIEW);
		}
	}

	if (cols) {
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
	}

	glDrawElements(GL_TRIANGLES, nprims*3, GL_UNSIGNED_SHORT, prims);
	GLERR();

	glDisableClientState(GL_VERTEX_ARRAY);

	if (cols)
		glDisableClientState(GL_COLOR_ARRAY);

	if (tex) {
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisable(tex->target);

		if (tex->target != GL_TEXTURE_RECTANGLE) {
			glMatrixMode(GL_TEXTURE);
			glLoadIdentity();
			glMatrixMode(GL_MODELVIEW);
		}
	}
}

bool get_colour(lua_State *L, int ctbl, float *col)
{
	static const char *channels[] = { "r", "g", "b", "a" };
	bool ret = false;
	int tbl;

	ctbl = absidx(L, ctbl);

	lua_pushliteral(L, "colour");
	lua_gettable(L, ctbl);
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_pushliteral(L, "color");
		lua_gettable(L, ctbl);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			return false;
		}
	}
	tbl = absidx(L, -1);

	for(int i = 0; i < 4; i++) {
		lua_pushstring(L, channels[i]);
		lua_gettable(L, tbl);

		// if indexing by r,b,g,a doesn't work, also try 1,2,3,4
		if (!lua_isnumber(L, -1)) {
			lua_pop(L, 1);
			lua_pushnumber(L, i+1);
			lua_gettable(L, -2);

			if (!lua_isnumber(L, -1)) {
				lua_pop(L, 1);
				continue;
			}
		}

		ret = true;
		col[i] = lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	return ret;
}

// Expect to see a table on the stack at idx;
// look for interesting elements
static void setstate(lua_State *L, int idx)
{
	float col[4] = { 1, 1, 1, 1 };

	int top = lua_gettop(L);

	if (idx < 0)
		idx = top + idx + 1;

	if (lua_gettop(L) < 1 || !lua_istable(L, idx))
		luaL_error(L, "state is not a table");

	if (get_colour(L, idx, col))
		glColor4fv(col);

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
		} else if (strcmp(str, "alpha") == 0) {
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_BLEND);
		} else
			luaL_error(L, "bad blend mode: must be one of none|add|alpha");
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
	{ "line",	gfx_line },
	{ "point",	gfx_point },
	{ "sprite",	gfx_sprite },

	// state setting
	{ "setstate",	gfx_setstate },
//	{ "getstate",	gfx_getstate },

	// texture constructor from PNG
	{ "texture",	texture_new_png },

	{0,0}
};

static bool get_number(lua_State *L, int idx, const char *field, 
		       float *retp, float defl)
{
	bool ok;

	lua_pushstring(L, field);
	lua_gettable(L, idx);
	*retp = defl;
	ok = lua_isnumber(L, -1) || lua_isnil(L, -1);

	if (lua_isnumber(L, -1))
		*retp = lua_tonumber(L, -1);
	  
	lua_pop(L, 1);

	return ok;
}

static bool get_matrix(lua_State *L, int idx,
		       float *a, float *b, float *c, float *d, 
		       float *tx, float *ty)
{
	idx = absidx(L, idx);

	if (!lua_istable(L, idx))
		return false;

	return get_number(L, idx, "a", a, 1) &&
		get_number(L, idx, "b", b, 0) &&
		get_number(L, idx, "c", c, 0) &&
		get_number(L, idx, "d", d, 1) &&
		get_number(L, idx, "x", tx, 0) &&
		get_number(L, idx, "y", ty, 0);
}

#define elem(r,c)	((c)*4 + (r))

// push the modelview and multiply a new transform:
// xform.mult matrix func args...
static int xform_mult(lua_State *L)
{
	float matrix[16] = {	// column major
		1, 0, 0, 0, 
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};

	if (!get_matrix(L, 1,
			&matrix[elem(0,0)], &matrix[elem(0,1)],
			&matrix[elem(1,0)], &matrix[elem(1,1)],
			&matrix[elem(0,3)], &matrix[elem(1,3)]))
		luaL_argerror(L, 1, "expecting matrix");

	luaL_argcheck(L, lua_isfunction(L, 2), 2, "expecting function");

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	GLERR();
	glMultMatrixf(matrix);

	lua_call(L, lua_gettop(L) - 2, LUA_MULTRET);

	glPopMatrix();

	return lua_gettop(L) - 1;
}

// push the modelview and load a new transform
// xform.push matrix func
static int xform_load(lua_State *L)
{
	float matrix[16] = {	// column major
		1, 0, 0, 0, 
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
	};
	float z[16];

	glGetFloatv(GL_MODELVIEW_MATRIX, z);

	if (!get_matrix(L, 1,
			&matrix[elem(0,0)], &matrix[elem(0,1)],
			&matrix[elem(1,0)], &matrix[elem(1,1)],
			&matrix[elem(0,3)], &matrix[elem(1,3)]))
		luaL_argerror(L, 1, "expecting matrix");

	luaL_argcheck(L, lua_isfunction(L, 2), 2, "expecting function");

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	GLERR();
	glLoadMatrixf(matrix);

	lua_call(L, lua_gettop(L) - 2, LUA_MULTRET);

	glPopMatrix();

	return lua_gettop(L) - 1;
}


static const luaL_reg xform_methods[] = {
	{ "mult",	xform_mult },
	{ "load",	xform_load },

	{0,0}
};

static void gfx_register(lua_State *L)
{
	luaL_openlib(L, "gfx", gfx_methods, 0);
	luaL_openlib(L, "xform", xform_methods, 0);

	luaL_newmetatable(L, "bokchoi.texture");
	luaL_openlib(L, NULL, texture_meta, 0);
}

/* ----------------------------------------------------------------------
   Lua setup
   ---------------------------------------------------------------------- */
static int panic(lua_State *L)
{
	lua_Debug debug;
	int depth;

	fprintf(stderr, "%s\n", lua_tostring(L, -1));

	for(depth = 1; lua_getstack(L, depth, &debug); depth++) {
		lua_getinfo(L, "nlS", &debug);
		printf("  %s:%d\n", debug.source, debug.currentline);
	}

	return 0;
}

void lua_setup(const char *src)
{
	static const luaL_Reg lualibs[] = {
		{"", luaopen_base},
		{LUA_LOADLIBNAME, luaopen_package},
		{LUA_TABLIBNAME, luaopen_table},
		{LUA_IOLIBNAME, luaopen_io},
		//{LUA_OSLIBNAME, luaopen_os},
		{LUA_STRLIBNAME, luaopen_string},
		{LUA_MATHLIBNAME, luaopen_math},
		//{LUA_DBLIBNAME, luaopen_debug},
		{NULL, NULL}
	};
	const luaL_Reg *lib = lualibs;
	int ret;
	lua_State *L;

	L = state = lua_open();

	lua_atpanic(L, panic);
	
	for (; lib->func; lib++) {
		lua_pushcfunction(L, lib->func);
		lua_pushstring(L, lib->name);
		lua_call(L, 1, 0);
	}

	tracker_register(state);
	gfx_register(state);
	mesh_register(state);
	text_register(state);

	const GLubyte *exts = glGetString(GL_EXTENSIONS);

	ext_texture_rect =
		gluCheckExtension((GLubyte *)"GL_ARB_texture_rectangle", exts) ||
		gluCheckExtension((GLubyte *)"GL_EXT_texture_rectangle", exts) ||
		gluCheckExtension((GLubyte *)"GL_NV_texture_rectangle", exts);
	GLERR();

	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &max_texture_units);
	GLERR();

	ext_generate_mipmap =
		GL_GENERATE_MIPMAP &&
		gluCheckExtension((GLubyte *)"GL_SGIS_generate_mipmap", exts);
	GLERR();
		
	ret = luaL_loadfile(L, src);
	if (ret) {
		const char *str = lua_tostring(L, -1);

		switch(ret) {
		case LUA_ERRFILE:
			fprintf(stderr, "file error: %s\n", 
				str);
			break;
		case LUA_ERRSYNTAX:
			fprintf(stderr, "syntax error: %s\n", 
				str);
			break;
			
		default:
			fprintf(stderr, "error loading %s: %s\n", 
				src, str);
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

void lua_frame(const unsigned char *img, int img_w, int img_h)
{
	// call process_frame, if any
	::img = (unsigned char *)img;
	::img_w = img_w;
	::img_h = img_h;

	GLERR();
	texture_new_frame(state, img, img_w, img_h, GL_LUMINANCE);
	//printf(">>> %d\n", lua_gettop(state));
	call_lua(state, 0, LUA_GLOBALSINDEX, "process_frame", "I", -1);
	lua_pop(state, 1);	// pop frame
	//printf("<<< %d\n", lua_gettop(state));

	GLERR();
	delete_pending();	// delete any textures which got gc-ed
}

bool vcall_lua(lua_State *L, int nret, int tblidx, const char *name, const char *args, va_list ap)
{
	int top = lua_gettop(L);
	int narg = 0;

	tblidx = absidx(L, tblidx);
	lua_pushstring(L, name);
	lua_gettable(L, tblidx);

	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	for(const char *cp = args; *cp; cp++) {
		narg++;
		switch(*cp) {
		case 's':	// string
			lua_pushstring(L, va_arg(ap, const char *));
			break;

		case 'r':	// reference
			lua_rawgeti(L, LUA_REGISTRYINDEX, va_arg(ap, int));
			break;

		case 'I': {	// stack index
			int idx = va_arg(ap, int);
			if (idx < 0 && idx > LUA_REGISTRYINDEX)
				idx = top + idx + 1;
			lua_pushvalue(L, idx);
			break;
		}

		case 'i':	// integer
			lua_pushnumber(L, va_arg(ap, int));
			break;

		case 'f':	// double
			lua_pushnumber(L, va_arg(ap, double));
			break;

		case 'n':	// nil
			lua_pushnil(L);
			break;

		default:
			printf("bad arg format %c\n", *cp);
			exit(1);
		}
	}

	lua_call(L, narg, nret);

	return true;
}

bool call_lua(lua_State *L, int nret, int tblidx, const char *name, const char *args, ...)
{
	bool ret;
	va_list ap;

	va_start(ap, args);
	ret = vcall_lua(L, nret, tblidx, name, args, ap);
	va_end(ap);

	return ret;
}
