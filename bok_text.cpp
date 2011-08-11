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

#include <list>

using namespace std;

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

	struct glyphtile *cache[256];
};

struct glyphatlas {
	GLuint texid_;

	GLuint size_;		// total size, square, power of 2
	GLuint tilesize_;	// tile size, square, power of 2

	// Used and freelist of tiles
	list<struct glyphtile *> used_;
	list<struct glyphtile *> free_;

	glyphatlas(unsigned order, unsigned tileorder);
	~glyphatlas();


	void initTexture(void);

	struct glyphtile *getTile();
	void freeTile(struct glyphtile *);
};

struct glyphtile {
	const GLuint offx_, offy_;		// x,y offset into the cache
	struct glyphatlas *const cache_;	// what cache we're part of

	// Glyph info
	struct face *face_;	// face_ non-NULL if glyph set
	unsigned index_;	// index into face

	unsigned w_, h_;	// glyph w/h
	int top_, left_;	// glyph offset
	float advance_;		// advance metric

	// tex coord left, right, top, bottom
	float tl_, tr_, tt_, tb_;

	glyphtile(GLuint x, GLuint y, struct glyphatlas *cache)
		: offx_(x), offy_(y), cache_(cache),
		  face_(NULL)
		{}
	void release();

	void setglyph(struct face *, unsigned index,
		      const FT_GlyphSlot slot);
};

void glyphatlas::initTexture()
{
	if (texid_ != (GLuint)~0)
		return;

	glGenTextures(1, &texid_);

	glBindTexture(GL_TEXTURE_2D, texid_);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY, size_, size_, 0,
		     GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

	GLERR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	GLERR();
}

glyphatlas::glyphatlas(unsigned order, unsigned tileorder)
	: texid_(~0),
	  size_(1 << order), tilesize_(1 << tileorder)
{
	size_ = 1 << order;
	tilesize_ = 1 << tileorder;

	for (unsigned y = 0; y < size_; y += tilesize_)
		for (unsigned x = 0; x < size_; x += tilesize_)
			free_.push_back(new glyphtile(x, y, this));
}

glyphatlas::~glyphatlas()
{
	//glDeleteTextures(1, &texid_);
}

struct glyphtile *glyphatlas::getTile()
{
	struct glyphtile *t;

	if (free_.empty()) {
		assert(!used_.empty());

		t = used_.front();
		used_.pop_front();
		t->release();
	} else {
		t = free_.front();
		free_.pop_front();
	}

	used_.push_back(t);

	return t;
}

void glyphatlas::freeTile(struct glyphtile *t)
{
	t->release();

	if (!t->face_)
		return;

	used_.remove(t);
	free_.push_front(t);
}

void glyphtile::release()
{
	if (face_)
		face_->cache[index_] = NULL;
	face_ = NULL;
}

void glyphtile::setglyph(struct face *face, unsigned index,
			 const FT_GlyphSlot slot)
{
	const FT_Bitmap *bm = &slot->bitmap;
	unsigned tsz = cache_->tilesize_;

	assert(face_ == NULL);
	face_ = face;
	index_ = index;

	w_ = bm->width;
	h_ = bm->rows;
	top_ = slot->bitmap_top;
	left_ = slot->bitmap_left;
	advance_ = slot->metrics.horiAdvance / 64.;

	tl_ = offx_ / (float)cache_->size_;
	tr_ = (offx_ + w_) / (float)cache_->size_;
	tb_ = offy_ / (float)cache_->size_;
	tt_ = (offy_ + h_) / (float)cache_->size_;

	GLubyte tex[tsz * tsz];
	memset(tex, 0, sizeof(tex));

	for(int y = 0; y < bm->rows; y++)
		for(int x = 0; x < bm->width; x++) {
			GLubyte b = bm->buffer[y * bm->pitch + x];
			tex[y * tsz + x] = (b * 255) / bm->num_grays;
		}

	cache_->initTexture();
	
	glBindTexture(GL_TEXTURE_2D, cache_->texid_);

	glTexSubImage2D(GL_TEXTURE_2D, 0,
			offx_, offy_, tsz, tsz,
			GL_LUMINANCE, GL_UNSIGNED_BYTE,
			tex);
	GLERR();
}

struct glyphcache {
	static const unsigned maxcachetile = 7;

	// Tile size is 2^idx
	struct glyphatlas *cachetiles[maxcachetile];

	glyphcache();
	~glyphcache();

	glyphtile *getTile(unsigned w, unsigned h);
};

static glyphcache cache;

glyphcache::glyphcache()
{
	for (unsigned order = 0; order < maxcachetile; order++)
		cachetiles[order] = new glyphatlas(10, order);
}

glyphcache::~glyphcache()
{
	for (unsigned order = 0; order < maxcachetile; order++)
		delete cachetiles[order];

}

static unsigned order2(unsigned x)
{
	unsigned ret = 0;

	while ((1 << ret) < x)
		ret++;

	return ret;
}

glyphtile *glyphcache::getTile(unsigned w, unsigned h)
{
	unsigned s = max(w,h);
	unsigned o = order2(s);

	if (o > maxcachetile)
		return NULL;

	return cachetiles[o]->getTile();
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
		struct glyphtile *g = face->cache[i];
		if (g && g->cache_)
			g->cache_->freeTile(g);
	}

	return 0;
}

static struct glyphtile *glyph_cache(struct face *face, unsigned char ch)
{
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

	glyphtile *g = cache.getTile(slot->bitmap.width, slot->bitmap.rows);

	g->setglyph(face, ch, slot);
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
		struct glyphtile *g = face->cache[*cp];

		if (g == NULL)
			g = glyph_cache(face, *cp);

		width += g->advance_;
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
		struct glyphtile *g = face->cache[*cp];

		if (g == NULL)
			g = glyph_cache(face, *cp);

		width += g->advance_;
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

	GLuint texid = ~0;

	bool inbegin = false;

	for(const unsigned char *cp = str; *cp != '\0'; cp++) {
		struct glyphtile *g = face->cache[*cp];

		if (g == NULL) {
			if (inbegin) {
				glEnd();
				inbegin = false;
			}
			g = glyph_cache(face, *cp);
		}

		if (!g || !g->cache_)
			continue;

		if (g->cache_->texid_ != texid) {
			texid = g->cache_->texid_;
			if (inbegin) {
				glEnd();
				inbegin = false;
			}
			glBindTexture(GL_TEXTURE_2D, texid);
		}

		float gx = (posx + g->left_ * scalex);
		float gy = (posy - g->top_ * fabs(scaley));
		float gw = g->w_ * scalex;
		float gh = g->h_ * fabs(scaley);

		if (!inbegin) {
			inbegin = true;
			glBegin(GL_QUADS);
		}

		glTexCoord2f(g->tl_, g->tb_);
		glVertex2f(gx, gy);

		glTexCoord2f(g->tr_, g->tb_);
		glVertex2f(gx + gw, gy);

		glTexCoord2f(g->tr_, g->tt_);
		glVertex2f(gx + gw, gy + gh);

		glTexCoord2f(g->tl_, g->tt_);
		glVertex2f(gx, gy + gh);

		posx += (g->advance_ * scalex);
	}

	if (inbegin)
		glEnd();

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
