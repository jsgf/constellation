// -*- c++ -*-
#ifndef _MISC_H
#define _MISC_H


#define GLERR() _glerr(__FILE__, __LINE__);

void _glerr(const char *file, int line);

enum Justify {
	JustLeft,
	JustRight,
	JustCentre
};

void drawString(float x, float y, Justify j, const char *fmt, ...);

#endif	/* MISC_H */
