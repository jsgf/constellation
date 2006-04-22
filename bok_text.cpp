/*
  Lua binding to FreeType
  
  Library: text
  Functions:
	face(path)	-- return a new FreeType face

  Objects:
	face
	face:glyph(ch, scale)

	glyph

*/

#include <assert.h>
#include <math.h>

#include <GL/glu.h>

#include <ft2build.h>
#include FT_FREETYPE_H

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "bok_lua.h"

static FT_Library ft_lib;

static const struct fterror
{
	int	err;
	const char *str;
} fterrors[] =
#undef __FTERRORS_H__
#define FT_ERROR_START_LIST {
#define FT_ERRORDEF(e, v, s) { e, s },
#define FT_ERROR_END_LIST };
#include <freetype/fterrors.h>
#define NERRS	(sizeof(fterrors)/sizeof(*fterrors))

static const char *ft_strerror(int err)
{
	for (unsigned i = 0; i < NERRS; i++)
		if (fterrors[i].err == err)
			return fterrors[i].str;

	return "???";
}

struct face {
	FT_Face ft_face;

	struct glyph *cache[256];
};

struct glyph {
	GLuint texid;

	int w, h;
	float th, tw;

	float advance;
	int top, left;
};

static unsigned power2(unsigned x)
{
	unsigned ret = 1;

	while(ret < x)
		ret <<= 1;

	return ret;
}

static int face_new(lua_State *L)
{
	struct face *face;
	const char *filename;
	FT_Error err;
	int ptsize, dpi;

	ptsize = 12;
	dpi = 72;

	if (!lua_isstring(L, 1))
		luaL_error(L, "need filename for face file");
	filename = lua_tostring(L, 1);

	if (lua_isnumber(L, 2))
		ptsize = (int)lua_tonumber(L, 2);

	if (lua_isnumber(L, 3))
		dpi = (int)lua_tonumber(L, 3);

	face = (struct face *)lua_newuserdata(L, sizeof(*face));
	luaL_getmetatable(L, "bokchoi.face");
	lua_setmetatable(L, -2);

	memset(face, 0, sizeof(*face));

	err = FT_New_Face(ft_lib, filename, 0, &face->ft_face);
	if (err != 0)
		luaL_error(L, "can't create face \"%s\": %s",
			   filename, ft_strerror(err));

	FT_Set_Char_Size(face->ft_face, ptsize * 64, 0, dpi, dpi);

	return 1;
}

static struct face *face_get(lua_State *L, int idx)
{
	struct face *f = (struct face *)luaL_checkudata(L, idx, "bokchoi.face");

	luaL_argcheck(L, f != NULL, idx, "'face' expected");

	return f;
}

static int face_gc(lua_State *L)
{
	struct face *face;

	if (!lua_isuserdata(L, 1))
		luaL_error(L, "face_fc: not userdata");

	face = face_get(L, 1);

	FT_Done_Face(face->ft_face);

	for(int i = 0; i < 256; i++) {
		struct glyph *g = face->cache[i];
		if (g != NULL) {
			glDeleteTextures(1, &g->texid);
			free(g);
		}
	}

	return 0;
}

static struct glyph *glyph_cache(struct face *face, unsigned char ch)
{
	struct glyph *g = (struct glyph*)malloc(sizeof(*g));

	if (g == NULL)
		return NULL;

	glGenTextures(1, &g->texid);

	FT_GlyphSlot slot = face->ft_face->glyph;
	FT_Error error;

	error  = FT_Load_Char(face->ft_face, ch,
			      FT_LOAD_NO_BITMAP |
			      FT_LOAD_NO_HINTING |
			      FT_LOAD_RENDER);
	if (error) {
		printf("load_char failed: %s\n", ft_strerror(error));
		return NULL;
	}

	FT_Bitmap bm = slot->bitmap;

	g->w = bm.width;
	g->h = bm.rows;

	g->advance = slot->metrics.horiAdvance / 64.;

	int tw = power2(g->w);
	int th = power2(g->h);

	g->left = slot->bitmap_left;
	g->top = slot->bitmap_top;

	GLubyte tex[tw * th];
	memset(tex, 0, sizeof(tex));

	for(int y = 0; y < g->h; y++)
		for(int x = 0; x < g->w; x++) {
			GLubyte b = bm.buffer[y * bm.pitch + x];
			tex[y * tw + x] = (b * 255) / bm.num_grays;
		}
	
	g->tw = (float)g->w / tw;
	g->th = (float)g->h / th;
		
	glBindTexture(GL_TEXTURE_2D, g->texid);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY, tw, th, 0, 
		     GL_LUMINANCE, GL_UNSIGNED_BYTE,
		     tex);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);		
	return g;
}

static int face_width(lua_State *L)
{
	struct face *face = face_get(L, 1);
	const unsigned char *str;
	float width;

	luaL_argcheck(L, lua_isstring(L, 2), 2, "Need string");

	str = (unsigned char *)lua_tostring(L, 2);

	width = 0;
	for(const unsigned char *cp = str; *cp != '\0'; cp++) {
		struct glyph *g = face->cache[*cp];

		if (g == NULL)
			g = glyph_cache(face, *cp);

		width += g->advance;
	}

	lua_pushnumber(L, width);

	return 1;
}

// face:draw(self, pt, [angle,] str)
static int face_draw(lua_State *L)
{
	struct face *face = face_get(L, 1);
	float x, y;
	float angle = 0;
	const unsigned char *str;
	float width;

	luaL_argcheck(L, get_xy(L, 2, &x, &y), 1, "Need coords");

	if (lua_isnumber(L, 3)) {
		angle = lua_tonumber(L, 3);
		str = (unsigned char *)lua_tostring(L, 4);
	} else
		str = (unsigned char *)lua_tostring(L, 3);

	width = 0;
	for(const unsigned char *cp = str; *cp != '\0'; cp++) {
		struct glyph *g = face->cache[*cp];

		if (g == NULL)
			g = glyph_cache(face, *cp);

		width += g->advance;
	}

	float posx = x;
	float posy = y;

	GLdouble proj[16], model[16];
	GLint viewport[4];

	glGetDoublev(GL_MODELVIEW_MATRIX, model);
	glGetDoublev(GL_PROJECTION_MATRIX, proj);
	glGetIntegerv(GL_VIEWPORT, viewport);

	float scalex, scaley;

	{
		GLdouble x0, y0, z0;
		GLdouble x1, y1, z1;

		gluProject(0, 0, 0, model, proj, viewport, &x0, &y0, &z0);
		gluProject(1, 1, 0, model, proj, viewport, &x1, &y1, &z1);

		scalex = 1. / (x1-x0);
		scaley = 1. / (y1-y0);
	}

	width *= scalex;
	glEnable(GL_TEXTURE_2D);
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	glTranslatef(posx, posy, 0);
	glRotatef(angle, 0, 0, 1);

	posx = posy = 0;

	posx -= width/2;

	for(const unsigned char *cp = str; *cp != '\0'; cp++) {
		struct glyph *g = face->cache[*cp];

		if (g == NULL)
			g = glyph_cache(face, *cp);

		if (g == NULL)
			continue;

		glBindTexture(GL_TEXTURE_2D, g->texid);

		float gx = (posx + g->left * scalex);
		float gy = (posy - g->top * fabs(scaley));
		float gw = g->w * scalex;
		float gh = g->h * fabs(scaley);

		glBegin(GL_TRIANGLE_FAN);
		  glTexCoord2f(0,0);
		  glVertex2f(gx, gy);

		  glTexCoord2f(g->tw, 0);
		  glVertex2f(gx + gw, gy);

		  glTexCoord2f(g->tw, g->th);
		  glVertex2f(gx + gw, gy + gh);

		  glTexCoord2f(0, g->th);
		  glVertex2f(gx, gy + gh);
		glEnd();

		posx += (g->advance * scalex);
	}

	glDisable(GL_TEXTURE_2D);

	glPopMatrix();
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

	return 0;
}

// mesh userdata meta
static const luaL_reg face_meta[] = {
	{ "__gc",	face_gc },
	{ "draw",	face_draw },
	{ "width",	face_width },

	{0,0}
};

static const luaL_reg text_library[] = {
	{ "face",	face_new },

	{0,0}
};

void text_register(lua_State *L)
{
	FT_Init_FreeType(&ft_lib);
	
	luaL_openlib(L, "text", text_library, 0);

	// face metatable
	luaL_newmetatable(L, "bokchoi.face");
	lua_pushstring(L, "__index");
	lua_pushvalue(L, -2);  /* pushes the metatable */
	lua_settable(L, -3);  /* metatable.__index = metatable */
  
	luaL_openlib(L, NULL, face_meta, 0);

	lua_pop(L, 2);

}
