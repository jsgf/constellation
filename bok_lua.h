// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

struct lua_State;

bool get_xy(lua_State *L, int idx, float *x, float *y);
int absidx(lua_State *L, int idx);

#endif // BOK_LUA_H
