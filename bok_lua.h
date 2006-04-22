// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

#include <stdarg.h>
#include <GL/gl.h>

// userdata texture structure
struct texture {
	GLuint texid;
	int width;
	int height;
	int texwidth, texheight;

	float tc[2*4];		// texture coord array

	GLenum format;		// texture format
	GLenum target;		// texture target
};


void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

struct lua_State;

bool get_colour(struct lua_State *L, int idx, float col[4]);
bool get_xy(struct lua_State *L, int idx, float *x, float *y);
int absidx(struct lua_State *L, int idx);

bool vcall_lua(struct lua_State *L, int nret, 
	       int tblidx, const char *func, const char *args, va_list);
bool call_lua(struct lua_State *L, int nret, 
	      int tblidx, const char *func, const char *args, ...);

struct texture;

struct texture *texture_get(lua_State *L, int idx);

struct texture *texture_init(struct texture *tex,
			     GLenum target, int width, int height, GLenum format);
struct texture *texture_alloc(lua_State *L,
			      GLenum target, int width, int height, GLenum format);
void texture_init_texcoord(struct texture *tex, float w, float h);

int texture_index(lua_State *L);
int texture_gc(lua_State *L);


// Render a mesh of triangles
void render_indexed_mesh(struct texture *tex, 
			 int nvert, 
			 float *coords, float *texcoords, GLubyte *cols,
			 int nprims, GLushort *prims);

#endif // BOK_LUA_H
