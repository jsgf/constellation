// -*- c++ -*-

// Playing with camera

#include <SDL/SDL.h>
#include <GL/glu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <errno.h>
#include <fcntl.h>

#include <valgrind/valgrind.h>

#include "Camera.h"
#if USE1394
#include "DC1394Camera.h"
#endif
#if USEV4L1
#include "V4LCamera.h"
#endif
#if USEV4L2
#include "V4L2Camera.h"
#endif

#include "bok_lua.h"

static Camera *cam;

static SDL_Surface *windowsurf;
static int screen_w, screen_h;
static void reshape(int w, int h);

static bool fullscreen = false;
static bool finished = false;
static bool paused = false;

static const char *record_base = NULL;

static const int BLOBSIZE = 64;
extern const char blob[BLOBSIZE*BLOBSIZE];
static GLuint blobtex;

static int newfile(const char *base, const char *ext)
{
	int fd;
	int count = 0;
	char buf[strlen(base)+strlen(ext)+4+1+1];

	while(count < 10000) {
		sprintf(buf, "%s%04d%s", base, count++, ext);
		fd = open(buf, O_CREAT|O_EXCL|O_WRONLY, 0660);
		if (fd == -1) {
			if (errno == EEXIST)
				continue;
			else
				return -1;
		} else
			return fd;
	}

	errno = EEXIST;
	return -1;
}


static struct vid_mode {
	int w, h;
} default_modes[] = {
	{ 128,  96 },		// SQSIF
	{ 160, 120 },		// QSIF
	{ 176, 144 },		// QCIF
	{ 320, 240 },		// SIF
	{ 352, 288 },		// CIF
	{ 640, 480 },		// VGA
	{ 800, 600 },
	{1024, 768 },
	{0,0}
};


static int cmp_mode(const struct vid_mode *a, const struct vid_mode *b)
{
#if 0
	int aa = a->w*a->h;
	int ab = b->w*b->h;

	if (aa < ab)
		return -1;
	else if (aa > ab)
		return 1;
	return 0;
#else
	if (a->w == b->w)
		return a->h - b->h;
	else
		return a->w - b->w;
#endif
}

static struct vid_mode *get_modes()
{
	SDL_Rect **m = SDL_ListModes(NULL, SDL_HWSURFACE | (fullscreen ? SDL_FULLSCREEN : 0));

	if (m == NULL || m == (SDL_Rect **)-1)
		return default_modes;

	int c;
	for(c = 0; m[c]; c++)
		;
	struct vid_mode *modes = new vid_mode[c + 1];
	for(int i = 0; i < c; i++) {
		modes[i].w = m[i]->w;
		modes[i].h = m[i]->h;
	}
	modes[c].w = modes[c].h = 0;

	qsort(modes, c, sizeof(modes[0]), (int (*)(const void *, const void *))cmp_mode);

	return modes;
}

static int mode_idx(const struct vid_mode *modes)
{
	int idx = 0;
	struct vid_mode cur = { screen_w, screen_h };

	for(idx = 0; modes[idx].w != 0; idx++)
		;

	idx--;

	for(; idx > 0; idx--) {
		//printf("modes[%d] = %dx%d\n", idx, modes[idx].w, modes[idx].h);
		if (cmp_mode(&modes[idx], &cur) <= 0)
			break;
	}

	//printf("idx %dx%d -> %d\n", screen_w, screen_h, idx);

	return idx;
}

static void free_modes(struct vid_mode *modes)
{
	if (modes != default_modes)
		delete[] modes;
}

static void reshape(int w, int h)
{
	unsigned flags = SDL_ANYFORMAT | SDL_OPENGL;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (w < 0 || w > 2000 || h < 0 || h > 2000) {
		printf("BAD WH %dx%d\n", w, h);
		return;
	}
	windowsurf = SDL_SetVideoMode(w, h, 32, flags);

	if (windowsurf == NULL) {
		printf("Can't set video mode to (%dx%d): %s\n", w, h, SDL_GetError());
		return;
	}


	float img_asp  = (float)cam->imageWidth() / cam->imageHeight();
	float screen_asp = (float)w / h;

	screen_w = w;
	screen_h = h;

	if (0)
		printf("img_asp=%g screen_asp=%g; screen=%dx%d; img=%dx%d\n",
		       img_asp, screen_asp,
		       w, h,
		       cam->imageWidth(), cam->imageHeight());

	if (img_asp < screen_asp) {
		int aw = (int)(h * img_asp);

		glViewport(w/2 - aw/2, 0, aw, h);
	} else {
		int ah = (int)(w / img_asp);

		glViewport(0, h/2 - ah/2, w, ah);
	}

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, cam->imageWidth(), cam->imageHeight(), 0);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

static void keyboard(SDL_keysym *sym)
{
	bool shift = !!(sym->mod & KMOD_SHIFT);

	switch(sym->sym) {

	case SDLK_MINUS:
	case SDLK_EQUALS:
	{
		int idx;

		struct vid_mode *modes = get_modes();

		if (shift) {
			if (sym->sym == SDLK_EQUALS) {
				for(idx = 0; modes[idx].w != 0; idx++)
					;
				idx--;
			} else
				idx = 0;
		} else {
			idx = mode_idx(modes);

			while(idx >= 0 && modes[idx].w != 0 && 
			      modes[idx].w == screen_w &&
			      modes[idx].h == screen_h) {
				if (sym->sym == SDLK_EQUALS)
					idx++;
				else
					idx--;
			}
			
		}
		if (idx < 0 || modes[idx].w == 0)
			break;
		reshape(modes[idx].w, modes[idx].h);

		free_modes(modes);

		break;
	}

	case SDLK_1:
	case SDLK_2:
	case SDLK_3:
	case SDLK_4:
	case SDLK_5:
	case SDLK_6:
	case SDLK_7:
	case SDLK_8:
	{
		int idx = sym->sym - SDLK_1;
		reshape(default_modes[idx].w, default_modes[idx].h);
		break;
	}

	case SDLK_e:
		fullscreen = !fullscreen;

		reshape(screen_w, screen_h);
		break;

	case SDLK_r:
		if (!cam->isrecording())
			cam->startRecord(newfile(record_base, ".y4m"));
		else
			cam->stopRecord();
		break;

	case SDLK_SPACE:
		paused = !paused;
		break;

	case SDLK_ESCAPE:
	case SDLK_q:
		finished = true;
		break;

	default:
		break;
	}
}

static void handle_events()
{
	SDL_Event ev;

	while(SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_KEYDOWN:
			keyboard(&ev.key.keysym);
			break;
		case SDL_QUIT:
			exit(0);
		}
	}
}

static unsigned long long get_now()
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec * 1000000ull + tv.tv_usec;
}

static void display(void)
{
	static const unsigned char *img;
	static unsigned int img_w, img_h;

	GLERR();

	if (!paused || img == NULL) {
		img = cam->getFrame();
		img_w = cam->imageWidth();
		img_h = cam->imageHeight();
	}

	glClearColor(.2, .2, .2, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	
	GLERR();

	lua_frame(img, img_w, img_h);

	GLERR();

	if (cam->isrecording()) {
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT |
			     GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT);

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glTranslatef(8, 8, 0);

		glBindTexture(GL_TEXTURE_2D, blobtex);

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glColor4f(1,0,0,1);

		const int size = 8;

		glBegin(GL_TRIANGLE_FAN);
		   glTexCoord2f(0, 0);
		   glVertex2f(0, 0);

		   glTexCoord2f(1, 0);
		   glVertex2f(size, 0);

		   glTexCoord2f(1, 1);
		   glVertex2f(size, size);

		   glTexCoord2f(0, 1);
		   glVertex2f(0, size);
		glEnd();

		glPopMatrix();
		glPopAttrib();
	}

	GLERR();

	SDL_GL_SwapBuffers();
}

int main(int argc, char **argv)
{
	int opt;
	bool err = false, cam_record = false;
	const char *camera_file = NULL;
	const char *script = "bok.lua";

	srandom(getpid());

	while((opt = getopt(argc, argv, "ep:Rr:")) != EOF) {
		switch(opt) {
		case 'e':
			fullscreen = true;
			break;

		case 'p':
			camera_file = optarg;
			break;

		case 'R':
			cam_record = true;
			break;

		case 'r':
			record_base = optarg;
			break;

		default:
			fprintf(stderr, "Unknown option '%c'\n", opt);
			err = true;
			break;
		}
	}

	if (err) {
		fprintf(stderr, "Usage: %s [-e] [-p recorded-data.y4m] [script.lua]\n",
			argv[0]);
		exit(1);
	}

	
	if (optind == argc-1)
		script = argv[optind];

	if (record_base == NULL) {
		const char *sb;
		char *rb, *cp;

		// chop of last piece of path, if present
		sb = strrchr(script, '/');
		if (sb == NULL)
			sb = script;
		else
			sb++;	// skip '/'

		// chop off extension
		rb = strdup(sb);
		cp = strrchr(rb, '.');
		if (cp != NULL)
			*cp = '\0';

		record_base = rb;
	}

	if (camera_file) {
		printf("opening %s...\n", camera_file);
		cam = new FileCamera(camera_file);
		cam->start();
	} else {
		Camera::framesize_t size = Camera::SIF;
		int fps = 30;

		cam = NULL;

#if USEV4L2
		if (!cam) {
			cam = new V4L2Camera(size, fps);
			if (!cam->start()) {
				delete cam;
				cam = NULL;
			}
		}
#endif

#if USE1394
		if (!cam) {
			cam = new DC1394Camera(size, fps);

			if (!cam->start()) {
				delete cam;
				cam = NULL;
			}
		}
#endif

#if USEV4L1
		if (!cam) {
			cam = new V4LCamera(size, fps);
			if (!cam->start()) {
				delete cam;
				cam = NULL;
			}
		}
#endif
		if (cam == NULL) {
			printf("No camera initialized\n");
			exit(1);
		}
	} 

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER |
		     SDL_INIT_NOPARACHUTE) == -1) {
		printf("Can't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	atexit(SDL_Quit);

	int rate = cam->getRate();

	if (cam_record)
		cam->startRecord(newfile(record_base, ".y4m"));

	SDL_WM_SetCaption("Constellation", "Constellation");
	SDL_ShowCursor(0);

	if (fullscreen) {
		struct vid_mode *m = get_modes();
		int idx;
		for(idx = 0; m[idx].w != 0; idx++)
			;
		idx--;
		reshape(m[idx].w, m[idx].h);
		free_modes(m);
	} else
		reshape(640, 480);

	GLint maxtex;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
	printf("max tex size %d\n", maxtex);

	glGenTextures(1, &blobtex);
	glBindTexture(GL_TEXTURE_2D, blobtex);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY,
		     BLOBSIZE, BLOBSIZE,
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, blob);

	lua_setup(script);
	atexit(lua_cleanup);

	if (0) {
		int samples;
		
		glEnable(GL_MULTISAMPLE);
		glGetIntegerv(GL_SAMPLE_BUFFERS, &samples);
		printf("samples=%d\n", samples);
	}

	while(!finished) {
		handle_events();
		display();

		static unsigned long long prev_time;

		if (prev_time != 0) {
			unsigned long long frame_delta = get_now() - prev_time; // time since prev call
			
			//printf("fps = %f\n", 1. / (frame_delta / 1000000.));
		}
		prev_time = get_now();
	}

	cam->stop();

	delete cam;
}
