// -*- c++ -*-
#ifndef _VAULT_OF_HEAVEN_H
#define _VAULT_OF_HEAVEN_H

#include <set>
#include <map>
#include <string>

#include "Graph.h"

class DrawnFeature;

class VaultOfHeaven {
	typedef DrawnFeature Star;

	static const int minConst = 3;

	class Constellation {
	public:
		typedef Graph<const Star *> StarGraph_t;
	private:
		StarGraph_t	stars_;
		const std::string	&name_;

	public:
		Constellation(const std::string &name);

		void draw() const;

		void addStars(const Star *, const Star *);
		bool removeStar(const Star *);

		StarGraph_t::EdgeMap_t::const_iterator begin() { return stars_.begin(); }
		StarGraph_t::EdgeMap_t::const_iterator end() { return stars_.begin(); }
		StarGraph_t::EdgeMap_t edges() { return stars_.edges(); }

		int size() const { return stars_.size(); }
	};

	typedef std::set<const Star *> FeatureSet_t;
	typedef std::map<const Star *, Constellation *> StarMap_t;
	typedef std::set<Constellation *> ConstellationSet_t;

	FeatureSet_t	stars_;		// all stars
	StarMap_t	starmap_;	// stars in constellations

	ConstellationSet_t	constellations_;

public:
	VaultOfHeaven();
	~VaultOfHeaven();

	void addStar(Star *);
	void removeStar(Star *);

	bool addConstellation();
	void removeConstellation(Constellation *c);

	void draw() const;

	void clear();

	int nConstellations() const { return constellations_.size(); }
};

#endif	// _VAULT_OF_HEAVEN_H
