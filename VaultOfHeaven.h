// -*- c++ -*-
#ifndef _VAULT_OF_HEAVEN_H
#define _VAULT_OF_HEAVEN_H

#include <set>
#include <map>
#include <string>

class DrawnFeature;

// Rearrange a pair of objects into a canonical order, so that
// pair<a,b> == pair<b,a>
template <typename T>
class unordered_pair: public std::pair<T,T>
{
public:
	unordered_pair(T &a, T &b)
		: std::pair<T,T>(a < b ? a : b, a < b ? b : a)
		{}
	virtual ~unordered_pair() {}

};

template<typename T>
bool operator==(const unordered_pair<T> a, const T &b) {
	return b == a.first || b == a.second;
}

template<typename T>
bool operator!=(const unordered_pair<T> a, const T &b) {
	return !(b == a.first || b == a.second);
}

class VaultOfHeaven {
	typedef DrawnFeature Star;

	static const unsigned minConst = 3;

	class Constellation {
	public:
		typedef unordered_pair<const Star *> StarPair_t;
		typedef std::set<StarPair_t> StarPairSet_t;

	private:
		StarPairSet_t		stars_;
		
		const std::string	&name_;

	public:
		Constellation(const std::string &name);

		void draw() const;

		void addStars(const Star *, const Star *);
		bool removeStar(const Star *);

		unsigned size() const { return stars_.size(); }
	};

	typedef std::pair<const Star *, Constellation *> StarConst_t;
	typedef std::map<const Star *, Constellation *> StarMap_t;
	typedef std::set<Constellation *> ConstellationSet_t;

	StarMap_t	starmap_;	// stars, and what
					// constellation each is in

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
