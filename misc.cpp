#include <SDL/SDL.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include "misc.h"

#define FONT "/usr/share/fonts/bitstream-vera/VeraBd.ttf"

void _glerr(const char *file, int line)
{
	GLenum err = glGetError();

	if (err != GL_NO_ERROR) {
		printf("GL error at %s:%d: %s\n",
		       file, line, (char *)gluErrorString(err));
	}
}

static int power2(unsigned x)
{
	unsigned ret = 1;

	while(ret < x)
		ret <<= 1;

	return ret;
}

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

static FT_Library ft_lib;
static FT_Face face;

static void do_init()
{
	FT_Init_FreeType(&ft_lib);
	FT_New_Face(ft_lib, FONT, 0, &face);
	FT_Set_Char_Size(face, 12 * 64, 0, 72, 0);
}

struct GlyphCache {
	GLuint	texid;

	int	_w, _h;
	float	_tw, _th;

	float	_advance;
	int	_top, _left;

	GlyphCache(unsigned char ch);
	~GlyphCache();
};

GlyphCache::GlyphCache(unsigned char ch)
{
	FT_Error error;

	glGenTextures(1, &texid);

	FT_GlyphSlot slot = face->glyph;
	error = FT_Load_Char(face, ch, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_RENDER);
	if (error) {
		printf("load_char failed: %s\n", ft_strerror(error));
		return;
	}

	FT_Bitmap bm = slot->bitmap;

	_w = bm.width;
	_h = bm.rows;

	_advance = slot->metrics.horiAdvance / 64.;

	int tw = power2(_w);
	int th = power2(_h);

	_left = slot->bitmap_left;
	_top = slot->bitmap_top;

	GLubyte tex[tw * th];
	memset(tex, 0, sizeof(tex));

	for(int y = 0; y < _h; y++)
		for(int x = 0; x < _w; x++) {
			GLubyte b = bm.buffer[y * bm.pitch + x];
			tex[y * tw + x] = (b * 255) / bm.num_grays;
		}
	
	_tw = (float)_w / tw;
	_th = (float)_h / th;
		
	glBindTexture(GL_TEXTURE_2D, texid);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY, tw, th, 0, GL_INTENSITY, GL_UNSIGNED_BYTE,
		     tex);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);		
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);		
}

GlyphCache::~GlyphCache()
{
	glDeleteTextures(1, &texid);
}

static GlyphCache *glyphcache[256];

void drawString(float x, float y, Justify just, const char *fmt, ...)
{
	static bool init_done = false;
	char buf[1000];
	va_list ap;

	if (!init_done) {
		do_init();
		init_done = true;
	}

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	float width = 0;
	for(unsigned char *cp = (unsigned char *)buf; *cp; cp++) {
		if (glyphcache[*cp] == NULL)
			glyphcache[*cp] = new GlyphCache(*cp);

		width += glyphcache[*cp]->_advance;
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

	switch(just) {
	case JustLeft:
		break;

	case JustRight:
		posx -= width;
		break;

	case JustCentre:
		posx -= width / 2;
		break;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_2D);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);

	//posx = floorf(posx);
	//posy = floorf(posy);
	for(const unsigned char *cp = (const unsigned char *)buf; *cp; cp++) {
		GlyphCache *gc = glyphcache[*cp];

		if (gc == NULL)
			continue;

		glBindTexture(GL_TEXTURE_2D, gc->texid);

		float gx = (posx + gc->_left * scalex);
		float gy = (posy - gc->_top * fabs(scaley));
		float gw = gc->_w * scalex;
		float gh = gc->_h * fabs(scaley);

		glBegin(GL_TRIANGLE_FAN);
		  glTexCoord2f(0,0);
		  glVertex2f(gx, gy);

		  glTexCoord2f(gc->_tw, 0);
		  glVertex2f(gx + gw, gy);

		  glTexCoord2f(gc->_tw, gc->_th);
		  glVertex2f(gx + gw, gy + gh);

		  glTexCoord2f(0, gc->_th);
		  glVertex2f(gx, gy + gh);
		glEnd();

		posx += (gc->_advance * scalex);
	}

	glDisable(GL_BLEND);	
	glDisable(GL_TEXTURE_2D);
}
