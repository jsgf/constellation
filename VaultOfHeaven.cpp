#include "VaultOfHeaven.h"
#include "DrawnFeature.h"
#include <cassert>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iterator>
#include "misc.h"

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
	for(StarMap_t::iterator it = starmap_.begin();
	    it != starmap_.end();
	    it++)
		it->second = 0;

	for(ConstellationSet_t::iterator it = constellations_.begin();
	    it != constellations_.end();
	    it++)
		delete(*it);

	constellations_.clear();
}

void VaultOfHeaven::addStar(DrawnFeature *f)
{
	assert(starmap_.count(f) == 0);
	starmap_[f] = 0;
}

void VaultOfHeaven::removeConstellation(Constellation *c)
{
	assert(constellations_.count(c) == 1);

	for(StarMap_t::const_iterator it = starmap_.begin();
	    it != starmap_.end();
	    it++)
		if (starmap_[it->first] == c)
			starmap_[it->first] = 0;

	constellations_.erase(c);
	delete c;
}

void VaultOfHeaven::removeStar(Star *f)
{
	if (starmap_.count(f) != 0) {
		Constellation *c = starmap_[f];

		if (c && c->removeStar(f))
			removeConstellation(c);

		starmap_.erase(f);
	}
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
	const vector<StarConst_t> v(starmap_.begin(), starmap_.end());

	const Star *prev = NULL;
	unsigned limit = v.size() / 2;

	while(limit--) {
		const StarConst_t &p = v[rand() % v.size()];

		if (p.second == 0) {
			prev = p.first;
			break;
		}
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
			if (starmap_[*it] != 0 && starmap_[*it] != c)
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
			if (starmap_[*it] != 0 && starmap_[*it] != c)
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
		//printf("constellation stillborn: %d\n", c->size());
		removeConstellation(c);
		return false;
	}

	return true;
}

VaultOfHeaven::Constellation::Constellation(const string &name)
	: name_(name)
{
}

void VaultOfHeaven::Constellation::draw() const
{
	float cx, cy;
	int count;

	glPushAttrib(GL_LINE_BIT);
	glLineWidth(2);
	glColor3f(.7, .7, .7);

	glBegin(GL_LINES);
	
	cx = cy = 0;
	count = 0;

	for(StarPairSet_t::const_iterator it = stars_.begin();
	    it != stars_.end();
	    it++) {
		cx += it->first->x();
		cy += it->first->y();
		count++;

		glVertex2f(it->first->x(), it->first->y());
		glVertex2f(it->second->x(), it->second->y());
	}

	glEnd();

	cx /= count;
	cy /= count;
	glColor3f(1, 1, 0);
	drawString(cx, cy, JustCentre, name_.c_str());

	glPopAttrib();
}

void VaultOfHeaven::Constellation::addStars(const Star *a, const Star *b)
{
	if (0)
		printf("adding a=%p b=%p\n", a, b);
	assert(a != NULL);
	assert(b != NULL);
	// if a is a neighbour of b, then b should be a neighbour of a
	assert(a->neighbours().count(b) != 0);
	assert(b->neighbours().count(a) != 0);

	StarPair_t p(a, b);

	if (stars_.count(p) == 0)
		stars_.insert(p);
	else if (0)
		printf("  duplicate\n");
}

bool VaultOfHeaven::Constellation::removeStar(const Star *a)
{
	if (0)
		printf("removing star %p\n", a);
	fflush(stdout);

	for(StarPairSet_t::iterator it = stars_.begin();
	    it != stars_.end();
	    it++)
		if (*it == a) {
			if (0)
				printf("  removing %p-%p\n", it->first, it->second);
			stars_.erase(it);
		}

	return stars_.size() < minConst;
}
