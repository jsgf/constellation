#include <SDL/SDL.h>
#include <GL/glu.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <set>
#include <map>
#include <fstream>
#include <fftw3.h>

#include <zlib.h>

#include <valgrind/valgrind.h>

#include "DrawnFeature.h"
#include "VaultOfHeaven.h"
#include "FeatureSet.h"
#include "Feature.h"
#include "Camera.h"
#include "DC1394Camera.h"
#include "Geom.h"
#include "misc.h"

extern "C" {
#include "klt.h"
}

static const int nFeatures = 80;

static bool finished = false;

static Camera *cam;
static GLuint imagetex;
static GLuint startex;
static int bordermode = 0;	// 0, 1, 2
static bool antishake = false;
static bool tracking = true;
static int histo = 0;		// 0, 1, 2
static bool normalize = false;
static int fft = 0;		// 0, 1, 2
static int fftscale = 2;	// 4-1 divide
static int markers = 1;		// show markers 0, 1, 2
static bool warp = false;
static bool paused = false;
static bool capture = false;
static bool autoconst = true;
static gzFile recordfile = NULL;
static bool fullscreen = false;
static bool overlay = true;

static SDL_Surface *windowsurf;
static int screen_w, screen_h;
static void reshape(int w, int h);

static VaultOfHeaven heaven;

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

static void atexit_closerecord(void)
{
	if (recordfile) {
		gzclose(recordfile);
		recordfile = NULL;
	}
}

class DrawnFeatureSet: public FeatureSet<DrawnFeature>
{
	friend class DrawnFeature;

	Triangulation *tri_;

	float off_x_, off_y_;

	typedef std::set<DrawnFeature *> FeatureSet_t;

protected:
	virtual Feature *newFeature(float x, float y, int val);
	virtual void removeFeature(Feature *f);

	void addToMesh(DrawnFeature *);

public:
	DrawnFeatureSet(int max, int min)
		: FeatureSet<DrawnFeature>(max, min), off_x_(0), off_y_(0)
		{
			tri_ = new Triangulation();

			if (0) {
				newFeature(0, 0, 0);
				newFeature(320, 0, 0);
				newFeature(320, 240, 0);
				newFeature(0, 240, 0);
			}
		}
	~DrawnFeatureSet() {
		delete tri_;
	}

	void update(const unsigned char *img, int w, int h);

	void draw() const;
	
	void zero() { off_x_ = off_y_ = 0; }
	float off_x() const { return off_x_; }
	float off_y() const { return off_y_; }
	
	const std::set<const DrawnFeature *> neighbours(const DrawnFeature *) const;

	void reTriangulate();
};

void DrawnFeatureSet::addToMesh(DrawnFeature *df)
{
	Locate_type loc;
	int idx;
	Face_handle f = tri_->locate(Point(df->x(), df->y()), loc, idx);
	if (f == 0 || loc != Triangulation::VERTEX) {
		Vertex_handle v = tri_->insert(Point(df->x() - off_x_,
						     df->y() - off_y_));

		df->setHandle(v);
		v->info() = df;
	}	
}

Feature *DrawnFeatureSet::newFeature(float x, float y, int val)
{
	DrawnFeature * df = new DrawnFeature(x, y, val, 0, this);

	heaven.addStar(df);
	
	return df;
}

void DrawnFeatureSet::removeFeature(Feature *f)
{
	DrawnFeature *df = static_cast<DrawnFeature *>(f);

	if (df->getHandle() != 0)
		tri_->remove(df->getHandle());

	heaven.removeStar(df);

	delete df;
}
void DrawnFeatureSet::update(const unsigned char *img, int w, int h) 
{
	FeatureSet<DrawnFeature>::update(img, w, h);

	float dx = 0, dy = 0;
	int tot = 0;

	for(FeatureVec_t::const_iterator it = begin();
	    it != end();
	    it++) {
		if (*it == NULL)
			continue;
		
		if ((*it)->getState() == Feature::Mature) {
			dx += (*it)->deltaX() * (*it)->val();
			dy += (*it)->deltaY() * (*it)->val();
			tot += (*it)->val();
		}
	}

	if (tot != 0) {
		off_x_ += dx/tot;
		off_y_ += dy/tot;
	}
}

void DrawnFeatureSet::draw() const
{
	if (warp) {
		glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, imagetex);
	
		if (0)
			glColor4f(1, .75, .75, .5);
		else
			glColor4f(1, 1, 1, 1);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		glBegin(GL_TRIANGLES);
		for(Finite_faces_iterator fi = tri_->finite_faces_begin();
		    fi != tri_->finite_faces_end();
		    fi++) {
			for(int i = 0; i < 3; i++) {
				Vertex_handle v = fi->vertex(i);
				DrawnFeature *f = v->info();

				if (1) {
					glTexCoord2f(v->point().x()+off_x_, v->point().y()+off_y_);
					glVertex2f(f->x(), f->y());
				} else {
					glTexCoord2f(f->x(), f->y());
					glVertex2f(v->point().x()+off_x_, v->point().y()+off_y_);
				}
			}
		}
		glEnd();
		glPopAttrib();
		GLERR();
	}

	if (markers == 2) {
		for(Finite_faces_iterator fi = tri_->finite_faces_begin();
		    fi != tri_->finite_faces_end();
		    fi++) {
			glColor3f(0,0,.25);
			glBegin(GL_LINE_LOOP);
			for(int i = 0; i < 3; i++) {
				Vertex_handle v = fi->vertex(i);
				DrawnFeature *f = v->info();
				glVertex2f(f->x(), f->y());
			}
			glEnd();
			
			if (1) {
				glColor3f(.25,0,.25);
				glBegin(GL_LINES);
				for(int i = 0; i < 3; i++) {
					Vertex_handle v = fi->vertex(i);
					DrawnFeature *f = v->info();
					glVertex2f(v->point().x()+off_x_, v->point().y()+off_y_);
					glVertex2f(f->x(), f->y());
				}
				glEnd();
			}
		}
	}

	for(FeatureVec_t::const_iterator it = begin();
	    it != end();
	    it++) {
		if (*it == NULL)
			continue;

		static_cast<DrawnFeature *>(*it)->draw();
	}
}

const std::set<const DrawnFeature *> DrawnFeatureSet::neighbours(const DrawnFeature *df) const
{
	std::set<const DrawnFeature *> ret;

	Vertex_handle v = df->getHandle();

	Vertex_circulator vc = tri_->incident_vertices(v);
	Vertex_circulator start(vc);

	if (vc != 0) {
		do {
			if (!tri_->is_infinite(vc))
				ret.insert(vc->info());
		} while(++vc != start);
	}

	return ret;
}

void DrawnFeatureSet::reTriangulate()
{
	delete tri_;
	tri_ = new Triangulation();

	for(FeatureVec_t::const_iterator it = begin();
	    it != end();
	    it++) {
		DrawnFeature *df = static_cast<DrawnFeature *>(*it);

		if (df == NULL || df->getState() != Feature::Mature)
			continue;

		Locate_type loc;
		int idx;

		Face_handle f = tri_->locate(Point(df->x(), df->y()), loc, idx);

		if (f == 0 || loc != Triangulation::VERTEX) {
			Vertex_handle v = tri_->insert(Point(df->x()-off_x_, df->y()-off_y_));

			v->info() = df;
			df->setHandle(v);
		} 
	}
}

static DrawnFeatureSet features(nFeatures, nFeatures /2);

void DrawnFeature::update(float x, float y)
{
	state_t st = getState();

	Feature::update(x, y);

	if (st == New && getState() == Mature)
		set_->addToMesh(this);
}

void DrawnFeature::draw() const
{
	if (state_ == Dead)
		return;

	float dx = features.windowWidth() / 2.f;
	float dy = features.windowHeight() / 2.f;

	if (markers == 2) {
		if (state_ == New) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(0, 1, 0, (float)age_ / Adulthood);
			
			dx *= Adulthood + 1 - age_;
			dy *= Adulthood + 1 - age_;
			glBegin(GL_LINES);
			glVertex2f(0, 0);
			glVertex2f(x_, y_);
			glEnd();
		} else {
			if (0)
				printf("val=%d logf(val)=%g\n",
				       val_, logf(val_));
		
			float rad = logf(val_);

			glEnable(GL_BLEND);

			glColor4f(0, 1, 1, .25);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBegin(GL_TRIANGLE_FAN);
			glVertex2f(x_-rad, y_-rad);
			glVertex2f(x_+rad, y_-rad);
			glVertex2f(x_+rad, y_+rad);
			glVertex2f(x_-rad, y_+rad);
			glEnd();
			GLERR();
			glDisable(GL_BLEND);

			glColor3f(1, 1, 0);
			glBegin(GL_LINES);
			glVertex2f(prev_x_, prev_y_);
			glVertex2f(x_, y_);
			glEnd();
		}

		glBegin(GL_LINE_LOOP);
			glVertex2f(x_ - dx, y_ - dy);
			glVertex2f(x_ + dx, y_ - dy);
			glVertex2f(x_ + dx, y_ + dy);
			glVertex2f(x_ - dx, y_ + dy);	
		glEnd();
	} else if (markers == 1) {
#if 0
		glPointSize(1);
		glColor3f(1, 1, 0);
		glBegin(GL_POINTS);
		glVertex2f(x_, y_);
		glEnd();
#else
		float rad = logf(val_);

		glPushAttrib(GL_ENABLE_BIT);

		glEnable(GL_BLEND);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, startex);
		GLERR();

		if (state_ == New) {
			float c = (float)age_ / Adulthood;

			glColor4f(c, c, c, c);
		} else
			glColor4f(1, 1, 1, 1);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();

		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBegin(GL_TRIANGLE_FAN);
		glTexCoord2f(0, 0);
		glVertex2f(x_-rad, y_-rad);

		glTexCoord2f(1, 0);
		glVertex2f(x_+rad, y_-rad);

		glTexCoord2f(1, 1);
		glVertex2f(x_+rad, y_+rad);

		glTexCoord2f(0, 1
);
		glVertex2f(x_-rad, y_+rad);
		glEnd();
		GLERR();

		glMatrixMode(GL_MODELVIEW);
		glPopAttrib();
		
#endif
	}		
	
	glDisable(GL_BLEND);
}

const DrawnFeature::DrawnFeatureSet_t DrawnFeature::neighbours() const
{
	return set_->neighbours(this);
}

static int power2(unsigned x)
{
	unsigned ret = 1;

	while(ret < x)
		ret <<= 1;

	return ret;
}

static void drawborder()
{
	if (bordermode == 0)
		return;

	glColor4f(.5, .5, .5, .5);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	if (bordermode == 1) {
		int in_x = features.borderInset_x();
		int in_y = features.borderInset_y();
		int cx = cam->imageWidth() / 2;
		int cy = cam->imageHeight() / 2;

		glBegin(GL_LINE_LOOP);
		glVertex2i(in_x, in_y);

		glVertex2i(cam->imageWidth() - in_x, in_y);
		
		glVertex2i(cam->imageWidth() - in_x, 
			   cam->imageHeight() - in_y);

		glVertex2i(in_x, cam->imageHeight() - in_y);
		glEnd();

		glBegin(GL_LINES);
		glVertex2i(cx - 5, cy);
		glVertex2i(cx + 5, cy);
		glVertex2i(cx, cy - 5);
		glVertex2i(cx, cy + 5);

		float ox = cx + features.off_x();
		float oy = cy + features.off_y();
		glColor3f(1, 0, 0);
		glVertex2f(ox - 5 , oy - 5);
		glVertex2f(ox + 5 , oy + 5);
		glVertex2f(ox - 5 , oy + 5);
		glVertex2f(ox + 5 , oy - 5);
		glEnd();

		drawString(cx, cy-10, 0, JustCentre,
			   "(%.2f,%.2f)", features.off_x(), features.off_y());

		GLERR();
	} else if (bordermode == 2) {
		int w = cam->imageWidth();
		int h = cam->imageHeight();
		int lx = features.borderInset_x();
		int ty = features.borderInset_y();
		int rx = w - lx;
		int by = h - ty;

		glColor4f(0,0,0,.5);
		glBegin(GL_TRIANGLE_STRIP);
			glVertex2i(0, 0);
			glVertex2i(lx, ty);
			glVertex2i(w, 0);
			glVertex2i(rx, ty);
			glVertex2i(w, h);
			glVertex2i(rx, by);
			glVertex2i(0, h);
			glVertex2i(lx, by);
			glVertex2i(0, 0);
			glVertex2i(lx, ty);
		glEnd();
	}

	glDisable(GL_BLEND);
}

void drawimage(const unsigned char *img, float deltax, float deltay)
{
	if (RUNNING_ON_VALGRIND)
		return;

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glTranslatef(-deltax, -deltay, 0);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindTexture(GL_TEXTURE_2D, imagetex);

	// XXX do something if camera output is larger than max texture size
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cam->imageWidth(), cam->imageHeight(),
			GL_LUMINANCE, GL_UNSIGNED_BYTE, img);

	GLERR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef(1.f / power2(cam->imageWidth()), 1.f / power2(cam->imageHeight()), 1);
	glMatrixMode(GL_MODELVIEW);

	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);

	glColor4f(1, 1, 1, 1);

	glShadeModel(GL_SMOOTH);
	
	glBegin(GL_QUADS);
	if (markers == 1)
		glColor4f(.04, .07, .51, 0); // zenith
	glTexCoord2i(0, 0);
	glVertex2i(0, 0);

	glTexCoord2i(cam->imageWidth(), 0);
	glVertex2i(cam->imageWidth(), 0);

	if (markers == 1)
		glColor4f(0, .58, .99, 0); // horizon
	glTexCoord2i(cam->imageWidth(), cam->imageHeight());
	glVertex2i(cam->imageWidth(), cam->imageHeight());

	glTexCoord2i(0, cam->imageHeight());
	glVertex2i(0, cam->imageHeight());
	glEnd();

	GLERR();
	glDisable(GL_TEXTURE_2D);

	glPopMatrix();
}


static void drawhisto(const unsigned char *img, int pixcount)
{
	unsigned h[256];
	memset(h, 0, sizeof(h));

	for(int i = 0; i < pixcount; i++)
		h[img[i]]++;

		
	glColor4f(0, 0, 0, .25);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBegin(GL_TRIANGLE_FAN);
	glVertex2i(0, 0);
	glVertex2i(256, 0);
	glVertex2i(256, 256);
	glVertex2i(0, 256);
	glEnd();

	glDisable(GL_BLEND);
	glColor3f(.75, .75, .75);
	glBegin(GL_LINE_LOOP);
	glVertex2i(0, 0);
	glVertex2i(256, 0);
	glVertex2i(256, 256);
	glVertex2i(0, 256);
	glEnd();

	unsigned max = 0;
	int peak = -1;
	for(int i = 0; i < 256; i++)
		if (h[i] > max) {
			max = h[i];
			peak = i;
		}

	glColor3f(1, 1, 1);
	glBegin(GL_LINE_STRIP);
	for(int x = 0; x < 256; x++) {
		float y;

		if (h[x] == 0) 
			y = 0;
		else {
			if (histo == 2)
				y = logf(h[x]) * 50 / logf(10);
			else
				y = h[x] * 256 / max;
		}
		glVertex2f(x, y);
	}
	glEnd();

	glColor3f(1, 1, 0);
	glBegin(GL_LINES);
	glVertex2i(peak, 0);
	glVertex2i(peak, 256);
	glEnd();

	drawString(peak+1, 1, 0, JustLeft, "%d", peak);
	drawString(2, 240, 0, JustLeft, histo == 1 ? "Linear" : "Log");

}

static void img_to_double(const unsigned char *img, int w, int h, int sx, int sy, double *d)
{
	for(int y = 0; y < h; y += sy) 
		for(int x = 0; x < w; x += sx) {
			*d = 0;
			for(int i = 0; i < sx; i++)
				for(int j = 0; j < sy; j++)
					*d += img[(y+j) * w + (x+i)];
			*d *= powf(-1, (x/sx)+(y/sy));
			d++;
		}
}

static void complex_to_img(const fftw_complex *cplx, bool power,
			   int w, int h,
			   float scale, unsigned char *rgba)
{
	float maxs = 0;

	for(int y = 0; y < h; y++) {
		const fftw_complex *row = &cplx[y * w];

		for(int x = 0; x < w; x++, row++, rgba += 4) {
			static const float cols[6][3] ={
				{ 1, 0, 0 },
				{ 1, 1, 0 },
				{ 0, 1, 0 },
				{ 0, 1, 1 },
				{ 0, 0, 1 },
				{ 1, 0, 1 },
			};
			float s = hypotf((*row)[0], (*row)[1]); // saturation

			if (s > maxs)
				maxs = s;

			if (!power) {
				s *= scale;
				if (s > 1)
					s = 1;
			} else
				s = log10f(s) / 7.;

			if (power) {
				rgba[0] = rgba[1] = rgba[2] = rgba[3] = (int)(s * 255);
			} else {
				float h = atan2f((*row)[0], (*row)[1]); // hue
				h += M_PI;
				h = h * 6 / (M_PI*2); // h = 0-6
		
				int idx0 = (int)h;
				int idx1 = (idx0+1) % 6;
				float p = h-idx0;

				float r = cols[idx0][0] * (1-p) + cols[idx1][0] * p;
				float g = cols[idx0][1] * (1-p) + cols[idx1][1] * p;
				float b = cols[idx0][2] * (1-p) + cols[idx1][2] * p;
				
				if (0)
					printf("cplx=%g,%g h=%g idx0=%d p=%g s=%g rgb=(%g,%g,%g)\n",
					       (*row)[0], (*row)[1], h, idx0, p, s, r, g, b);
				
				rgba[0] = (int)(r * s * 255);
				rgba[1] = (int)(g * s * 255);
				rgba[2] = (int)(b * s * 255);
				rgba[3] = (int)(s * 255);
			}
		}
	}

	if (0)
		printf("maxs=%g scaled=%g, log(maxs)=%g, scaled(log(maxs))=%g\n", maxs, maxs*scale, log10(maxs), log10(maxs)*(256./7));
}

static void drawfft(const unsigned char *img, int w, int h, int scale)
{
	static int saved_scale;
	static GLuint ffttex = 0;
	static fftw_plan plan;
	static double *in;
	static fftw_complex *out;
	const int fw = ((w/fftscale)/2)+1;
	const int fh = (h/fftscale);

	if (ffttex == 0) {
		glGenTextures(1, &ffttex);

		glBindTexture(GL_TEXTURE_2D, ffttex);
		glTexImage2D(GL_TEXTURE_2D, 0,
			     GL_RGBA, 
			     power2(fw*fftscale), power2(fh*fftscale),
			     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		GLERR();

		in = (double *)fftw_malloc(sizeof(*in) * w * h);
		out = (fftw_complex *)fftw_malloc(sizeof(*out) * (fw * fftscale) * (fh * fftscale));
	}

	if (plan == NULL || scale != saved_scale) {
		if (plan != NULL)
			fftw_destroy_plan(plan);
		plan = fftw_plan_dft_r2c_2d(h/fftscale, w/fftscale, in, out, FFTW_MEASURE);

		saved_scale = scale;
	}

	img_to_double(img, w, h, fftscale, fftscale, in);
	fftw_execute(plan);

	unsigned char tex[fw * fh * 4];
	complex_to_img(out, fft == 2,
		       fw, fh,
		       256./(w*h), tex);

	glBindTexture(GL_TEXTURE_2D, ffttex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GLERR();

	//printf("w/h=%dx%d fw/fh=%dx%d, tex %dx%d\n", w, h, fw, fh, fw/fftscale, fh/fftscale);
	glTexSubImage2D(GL_TEXTURE_2D, 0,
			0, 0, fw, fh,
			GL_RGBA, GL_UNSIGNED_BYTE, tex);
	GLERR();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef((float)(fw-1) / power2(fw*fftscale), (float)(fh-1) / power2(fh*fftscale), 1);
	glMatrixMode(GL_MODELVIEW);

	glEnable(GL_TEXTURE_2D);
	glColor4f(1, 1, 1, 1);
	glBegin(GL_QUADS);
	glTexCoord2i(0, 0);
	glVertex2i(0, 0);

	glTexCoord2i(1, 0);
	glVertex2i(fw, 0);

	glTexCoord2i(1, 1);
	glVertex2i(fw, fh);

	glTexCoord2i(0, 1);
	glVertex2i(0, fh);


	glTexCoord2i(1, 1);
	glVertex2i(fw, 0);

	glTexCoord2i(0, 1);
	glVertex2i(fw+fw, 0);

	glTexCoord2i(0, 0);
	glVertex2i(fw+fw, fh);

	glTexCoord2i(1, 0);
	glVertex2i(fw, fh);

	glEnd();
	GLERR();

	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
}

static void display(void)
{
	static struct timeval prev;
	struct timeval start;
	float deltax = 0, deltay = 0;
	static const unsigned char *img;
	int active = 0;

	if (!paused || img == NULL)
		img = cam->getFrame();

	gettimeofday(&start, NULL);

	glClearColor(.2, .2, .2, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	// contrast expansion
	if (normalize) {
		static unsigned char *newimg;
		int w = cam->imageWidth();
		int h = cam->imageHeight();
		int bottom, top, accum;
		unsigned hist[256];

		memset(hist, 0, sizeof(hist));

		if (newimg == NULL)
			newimg = new unsigned char[w*h];

		for(int i = 0; i < w*h; i++)
			hist[img[i]]++;
		for(accum = bottom = 0; bottom < 256; bottom++) {
			accum += hist[bottom];
			if (accum > 100)
				break;
		}
		for(accum = 0, top = 255; top > bottom; top--) {
			accum += hist[top];
			if (accum > 500)
				break;
		}

		for(int i = 0; i < w*h; i++) {
			int p = img[i];
			p -= bottom;
			p = (p * 256) / (top-bottom);

			if (p > 255)
				p = 255;
			if (p < 0)
				p = 0;

			newimg[i] = p;
		}

		img = newimg;
	}

	// image capture to pgm file
	if (capture) {
		char buf[100];
		FILE *fp = NULL;

		int fd = newfile("capture", ".pgm");

		if (fd != -1)
			fp = fdopen(fd, "wb");

		if (fp != NULL) {
			fprintf(fp, "P5\n%d %d 255\n",
				cam->imageWidth(), cam->imageHeight());
			fwrite(img, cam->imageWidth(), cam->imageHeight(), fp);
			fclose(fp);

			printf("wrote %s\n", buf);
		}

		capture = false;
	}

	drawimage(img, deltax, deltay);

	// feature tracking
	if (tracking) {
		features.update(img, cam->imageWidth(), cam->imageHeight());
		active = features.nFeatures();

#if 0
		if (antishake) {
			for(int i = 0; i < nFeatures; i++) {
				if (features[i].getState() == Feature::Mature) {
					deltax += features[i].deltaX();
					deltay += features[i].deltaY();
				}
			}
			deltax /= active;
			deltay /= active;
		}
#endif
	}

	// tracking boundary
	drawborder();

	// draw the constellations
	if (autoconst) {
		features.reTriangulate();
		heaven.addConstellation();
	}
	heaven.draw();

	// more tracking
	features.draw();

	if (0)
		printf("active = %d, limit=%d\n", active, nFeatures * 3 / 4);

	// display fft of image contents
	if (fft)
		drawfft(img, cam->imageWidth(), cam->imageHeight(), fftscale);

	// show histogram
	if (histo) {
		int pixcount = cam->imageWidth() * cam->imageHeight();

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();

		glLoadIdentity();
		glTranslatef(40, 40, 0);
		glScalef(.25, .25, 1);

		drawhisto(img, pixcount);

		glPopMatrix();
	}

	// timing stats
	struct timeval now;
	unsigned long delta, framedelta;

	gettimeofday(&now, NULL);

	delta = ((now.tv_sec * 1000000 + now.tv_usec) -
		 (start.tv_sec * 1000000 + start.tv_usec)) / 1000;
	framedelta = ((now.tv_sec * 1000000 + now.tv_usec) -
		      (prev.tv_sec * 1000000 + prev.tv_usec)) / 1000;
	prev = now;

	if (overlay) {
		glColor3f(1, 1, 0);
		drawString(10, cam->imageHeight() - 12, 0, JustLeft,
			   "Frame time: %3dms; %2d fps; %2d/%d active features", 
			   delta, 1000 / framedelta, active, nFeatures);

		drawString(10, 10, 0, JustLeft, "%dx%d", screen_w, screen_h);

		if (recordfile) {
			glColor3f(1, 0, 0);
			drawString(cam->imageWidth() - 12, 10, 0, JustRight,
				   "Recording");
		}
		if (cam->isrecording()) {
			glColor3f(1, 0, 0);
			drawString(cam->imageWidth() - 12, 20, 0, JustRight,
				   "Recording Camera");
		}
	}

	if (recordfile) {
		unsigned char *pix = new unsigned char [screen_w * screen_h * 3];
		struct timeval start, end;

		glPixelStorei(GL_PACK_ROW_LENGTH, 0);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		gettimeofday(&start, NULL);
		glReadPixels(0, 0, screen_w, screen_h,
			     GL_RGB, GL_UNSIGNED_BYTE,
			     pix);
		gettimeofday(&end, NULL);
		printf("glReadPixels took %u ms\n",
		       (unsigned)((end.tv_sec*1000000ull+end.tv_usec)-(start.tv_sec*1000000ull+start.tv_usec)) / 1000);
		int rows = screen_h & ~1; // even number of rows for mpeg
		
		char hdr[100];

		snprintf(hdr, sizeof(hdr), "P6\n%d %d 255\n", screen_w, rows);
		gzwrite(recordfile, hdr, strlen(hdr));

		int wb = screen_w * 3;
		
		for (int r = rows - 1; r >= 0; r--)
			gzwrite(recordfile, pix + (r * wb), wb);
		
		gzflush(recordfile, Z_SYNC_FLUSH);

		delete[] pix;
	}

	SDL_GL_SwapBuffers();
}

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

	case SDLK_ESCAPE:
	case SDLK_q:
		finished = true;
		break;

	case SDLK_b:
		bordermode = (bordermode + 1) % 3;
		break;

	case SDLK_t:
		if (shift)
			features.reTriangulate();
		else
			tracking = !tracking;
		break;

	case SDLK_n:
		normalize = !normalize;
		break;

	case SDLK_o:
		overlay = !overlay;
		break;

	case SDLK_r:
		if (shift) {
			if (!cam->isrecording())
				cam->startRecord(newfile("camera", ".y4m"));
			else
				cam->stopRecord();
		} else {
			if (recordfile == NULL)
				recordfile = gzdopen(newfile("record", ".ppm.gz"), "wb2");
			else {
				gzclose(recordfile);
				recordfile = NULL;
			}
		}
		break;
			
	case SDLK_h:
		histo = (histo + 1) % 3;
		break;

	case SDLK_f:
		if (shift) {
			fftscale--;
			if (fftscale == 0)
				fftscale = 4;
		} else 
			fft = (fft + 1) % 3;
		break;

	case SDLK_m:
		markers = (markers+1)%3;
		break;

	case SDLK_w:
		warp = !warp;
		break;

	case SDLK_p:
		paused = !paused;
		break;

	case SDLK_a:
		autoconst = !autoconst;
		break;

	case SDLK_c:
		if (!shift) {
			const int limit = 10;
			int i;
			
			features.reTriangulate();
			for(i = 0; i < limit; i++)
				if (heaven.addConstellation())
					break;
			if (i == limit) {
				heaven.clear();
				features.reTriangulate();
				heaven.addConstellation();
			}
		} else
			heaven.clear();
		break;

	case SDLK_s:
		if (!shift)
			antishake = !antishake;
		else
			capture = true;
		break;

	case SDLK_z:
		features.zero();
		break;

	default:
		break;
	}
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
	gluOrtho2D(0, cam->imageWidth(), 0, cam->imageHeight());
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glScalef(1, -1, 1);
	glTranslatef(0, -cam->imageHeight(), 0);
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

int main(int argc, char **argv)
{
	int opt;
	bool err = false, cam_record = false;

	srandom(getpid());

	while((opt = getopt(argc, argv, "rRaeto")) != EOF) {
		switch(opt) {
		case 'r':
			recordfile = gzdopen(newfile("record", ".ppm.gz"), "wb2");
			break;

		case 'R':
			cam_record = true;
			break;

		case 'a':
			autoconst = false;
			break;

		case 't':
			tracking = false;
			break;

		case 'e':
			fullscreen = true;
			break;

		case 'o':
			overlay = false;
			break;

		default:
			fprintf(stderr, "Unknown option '%c'\n", opt);
			err = true;
			break;
		}
	}

	if (err) {
		fprintf(stderr, "Usage: %s [-rRaeto] [recorded-data.y4m]\n",
			argv[0]);
		exit(1);
	}

	
	if (optind == argc-1) {
		printf("opening %s...\n", argv[optind]);
		cam = new FileCamera(argv[optind]);
	} else {
		Camera::framesize_t size = RUNNING_ON_VALGRIND ? Camera::QSIF : Camera::SIF;
		int fps = RUNNING_ON_VALGRIND ? 15 : 30;

		cam = new DC1394Camera(size, fps);
		if (!cam->start()) {
			delete cam;
			cam = new V4LCamera(size, fps);
			cam->start(); // will use test pattern if failed
		}
	} 

	atexit(atexit_closerecord);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == -1) {
		printf("Can't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	atexit(SDL_Quit);

	int rate = cam->getRate();

	if (cam_record)
		cam->startRecord(newfile("camera", ".y4m"));

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

	glGenTextures(1, &imagetex);
	glBindTexture(GL_TEXTURE_2D, imagetex);
	GLERR();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
		     power2(cam->imageWidth()), power2(cam->imageHeight()),
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	GLERR();

	glGenTextures(1, &startex);
	glBindTexture(GL_TEXTURE_2D, startex);
	GLERR();

	static const int SIZE = 64;
	extern const char star[SIZE*SIZE];

	glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY,
		     SIZE, SIZE,
		     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, star);

	GLERR();

	while(!finished) {
		handle_events();
		display();

		if (autoconst && random() < (RAND_MAX/1000))
			heaven.clear();

		// Timing: 
		// We want time_between_calls + sleep_here == (1/rate_)
		// We can't control anything except sleep_here
		static unsigned long long prev_time;
		static long sleep_time = 1000000 / 30;
		long want_time = 1000000 / rate;

		if (prev_time != 0) {
			unsigned long long frame_delta = get_now() - prev_time; // time since prev call
			
			long error = (frame_delta + sleep_time) - want_time;
			
			sleep_time -= error / 10;

			if (sleep_time > 0)
				usleep(sleep_time);
			else
				sleep_time = 0;	// clamp
		}
		prev_time = get_now();
	}

	cam->stop();

	delete cam;
}
