// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

struct lua_State;

void *userdata_get(struct lua_State *, int idx, 
		   int (*gc)(struct lua_State *), const char *name);

#endif // BOK_LUA_H
