#include <GL/glut.h>
#include <stdio.h>
#include <stdarg.h>
#include "misc.h"

void _glerr(const char *file, int line)
{
	GLenum err = glGetError();

	if (err != GL_NO_ERROR) {
		printf("GL error at %s:%d: %s\n",
		       file, line, (char *)gluErrorString(err));
	}
}

void drawString(float x, float y, Justify j, const char *fmt, ...)
{
	char buf[1000];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	float w = glutBitmapLength(GLUT_BITMAP_HELVETICA_12, (unsigned char *)buf);
	switch(j) {
	case JustLeft:
		break;
	case JustRight:
		x -= w;
		break;

	case JustCentre:
		x -= w/2;
		break;
	}

	glRasterPos2f(x, y);

	for(char *cp = buf; *cp; cp++)
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *cp);
}
