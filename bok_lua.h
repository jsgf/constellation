// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

#include <stdarg.h>

void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

struct lua_State;

bool get_xy(struct lua_State *L, int idx, float *x, float *y);
int absidx(struct lua_State *L, int idx);

void vcall_lua(struct lua_State *L, int nret, int func, const char *args, va_list);
void call_lua(struct lua_State *L, int nret, int func, const char *args, ...);

#endif // BOK_LUA_H
