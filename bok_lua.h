// -*- c++ -*-

#ifndef BOK_LUA_H
#define BOK_LUA_H

extern const unsigned char *img;
extern unsigned int img_w, img_h;

void lua_setup(const char *src);
void lua_frame();
void lua_cleanup();

#endif // BOK_LUA_H
