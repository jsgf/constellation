// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

#include <stdarg.h>
#include <GL/gl.h>

void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

struct lua_State;

bool get_xy(struct lua_State *L, int idx, float *x, float *y);
int absidx(struct lua_State *L, int idx);

bool vcall_lua(struct lua_State *L, int nret, 
	       int tblidx, const char *func, const char *args, va_list);
bool call_lua(struct lua_State *L, int nret, 
	      int tblidx, const char *func, const char *args, ...);

struct texture;

struct texture *texture_get(lua_State *L, int idx);

// Render a mesh of triangles
void render_indexed_mesh(struct texture *tex, 
			 int nvert, 
			 float *coords, float *texcoords,
			 int nprims, GLushort *prims);

#endif // BOK_LUA_H
