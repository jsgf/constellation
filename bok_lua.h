// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

void lua_setup(const char *src);
void lua_frame(const unsigned char *img, int width, int height);
void lua_cleanup();

#endif // BOK_LUA_H
