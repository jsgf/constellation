#include "VaultOfHeaven.h"
#include "DrawnFeature.h"
#include <cassert>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iterator>

using namespace std;

#include <GL/gl.h>

static class NameList
{
	vector<string> names_;

public:
	NameList(const string &filename);

	const string &getName();
} namelist("names.txt");

NameList::NameList(const string &filename)
{
	ifstream f(filename.c_str());;
	string line;

	while(getline(f, line, '\n'))
		names_.push_back(line);

	if (names_.size() == 0)
		names_.push_back(string(""));
}

const string &NameList::getName()
{
	return names_[rand() % names_.size()];
}

VaultOfHeaven::VaultOfHeaven()
{
}

VaultOfHeaven::~VaultOfHeaven()
{
	clear();
}

void VaultOfHeaven::clear()
{
	for(ConstellationSet_t::iterator it = constellations_.begin();
	    it != constellations_.end();
	    it++)
		delete(*it);

	starmap_.clear();
	constellations_.clear();
}

void VaultOfHeaven::addStar(DrawnFeature *f)
{
	stars_.insert(f);
}

void VaultOfHeaven::removeConstellation(Constellation *c)
{
	assert(constellations_.count(c) == 1);

	for(StarMap_t::const_iterator it = starmap_.begin();
	    it != starmap_.end();
	    it++)
		if (starmap_[it->first] == c)
			starmap_.erase(it->first);
	constellations_.erase(c);
	delete c;
}

void VaultOfHeaven::removeStar(Star *f)
{
	if (starmap_.count(f) != 0) {
		Constellation *c = starmap_[f];

		if (c->removeStar(f))
			removeConstellation(c);

		starmap_.erase(f);
	}
	stars_.erase(f);
}

void VaultOfHeaven::draw() const
{
	for(ConstellationSet_t::const_iterator it = constellations_.begin();
	    it != constellations_.end();
	    it++)
		(*it)->draw();
}

bool VaultOfHeaven::addConstellation()
{
	const vector<const Star *> v(stars_.begin(), stars_.end());

	const Star *prev = NULL;
	int limit = v.size() * 2;

	while(limit--) {
		prev = v[rand() % v.size()];
		if (prev && starmap_.count(prev) == 0)
			break;
	}

	if (prev == NULL) {
		printf("can't find a star to start with\n");
		return false;
	}

	Constellation *c = new Constellation(namelist.getName());

	const Star *star = NULL;

	starmap_[prev] = c;
	limit = 0;
	do {
		Star::DrawnFeatureSet_t next = prev->neighbours();

		int sum = 0;

		star = NULL;
		for(Star::DrawnFeatureSet_t::const_iterator it = next.begin();
		    it != next.end();
		    it++) {
			if (starmap_.count(*it) != 0 && starmap_[*it] != c)
				continue;

			sum += (*it)->val();
		}

		if (sum == 0)
			break;

		int r = rand() % sum;
		sum = 0;
		for(Star::DrawnFeatureSet_t::const_iterator it = next.begin();
		    it != next.end();
		    it++) {
			if (starmap_.count(*it) != 0 && starmap_[*it] != c)
				continue;

			if (r < sum) {
				star = *it;
				break;
			}

			sum += (*it)->val();
		}

		if (star == NULL)
			break;

		c->addStars(prev, star);
		starmap_[star] = c;
		prev = star;
	} while(limit++ < minConst || (rand() < int(RAND_MAX * .8)) );

	constellations_.insert(c);

	if (c->size() < minConst) {
		printf("constellation stillborn: %d\n", c->size());
		removeConstellation(c);
		return false;
	}

	return true;
}

VaultOfHeaven::Constellation::Constellation(const string &name)
	: name_(name)
{
}

void drawString(float x, float y, const char *fmt, ...);

void VaultOfHeaven::Constellation::draw() const
{
	float cx, cy;
	int count;

	glPushAttrib(GL_LINE_BIT);
	glLineWidth(2);
	glColor3f(1, 1, 0);

	glBegin(GL_LINES);
	
	cx = cy = 0;
	count = 0;

	for(StarGraph_t::EdgeMap_t::const_iterator it = stars_.begin();
	    it != stars_.end();
	    it++) {
		cx += it->first->x();
		cy += it->first->y();
		count++;

		for(StarGraph_t::VertexSet_t::const_iterator vi = it->second.begin();
		    vi != it->second.end();
		    vi++) {
			glVertex2f(it->first->x(), it->first->y());
			glVertex2f((*vi)->x(), (*vi)->y());
		}
	}

	glEnd();

	cx /= count;
	cy /= count;
	drawString(cx, cy, name_.c_str());

	glPopAttrib();
}

void VaultOfHeaven::Constellation::addStars(const Star *a, const Star *b)
{
	printf("a=%p b=%p\n", a, b);
	assert(a != NULL);
	assert(b != NULL);
	//assert(a->neighbours().count(b) != 0);
	//assert(b->neighbours().count(a) != 0);

	stars_.insert(a, b);
}

bool VaultOfHeaven::Constellation::removeStar(const Star *a)
{
	stars_.erase(a);

	return stars_.size() < minConst;
}
