// -*- c++ -*-

#ifndef _GRAPH_H
#define _GRAPH_H

#include <set>
#include <map>

template <typename T>
class Graph
{
public:
	typedef std::set<T> VertexSet_t;
	typedef std::map<T, VertexSet_t> EdgeMap_t;

private:
	EdgeMap_t edges_;
	
	void _insert(T &from, T &to) {
		if (edges_.count(from) == 0) {
			VertexSet_t s;
			s.insert(to);

			edges_.insert(make_pair(from, s));
		} else if (edges_[from].count(to) == 0)
			edges_[from].insert(to);
	}
			
public:
	void insert(T &from, T &to) {
		_insert(from, to);
		_insert(to, from);
	}

	void erase(T &from) {
		if (edges_.count(from) == 0)
			return;

		for(typename VertexSet_t::iterator it = edges_[from].begin();
		    it != edges_[from].end();
		    it++) {
			const T &to = *it;

			edges_[to].erase(from);
		}
		edges_.erase(from);
	}

	typename EdgeMap_t::const_iterator begin() const { return edges_.begin(); }
	typename EdgeMap_t::const_iterator end() const { return edges_.end(); }
	EdgeMap_t edges() const { return edges_; }

	void clear() {
		edges_.clear();
	}

	int size() const {
		return edges_.size();
	}
};

#endif	/* GRAPH_H */
