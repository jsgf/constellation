#ifndef _DRAWNFEATURE_H
#define _DRAWNFEATURE_H

#include "Feature.h"
#include "Geom.h"

class DrawnFeatureSet;

class DrawnFeature: public Feature
{
	Vertex_handle	handle_;

	DrawnFeatureSet *set_;
	
public:
	DrawnFeature(float x, float y, int val, Vertex_handle handle = 0, DrawnFeatureSet *set = 0)
		: Feature(x, y, val), handle_(handle), set_(set)
	{}

	void draw() const;
	void update(float, float);

	Vertex_handle &getHandle() { return handle_; }
	const Vertex_handle &getHandle() const { return handle_; }
	void setHandle(Vertex_handle h) { handle_ = h; }

	typedef std::set<const DrawnFeature *> DrawnFeatureSet_t;

	const DrawnFeatureSet_t neighbours() const;
};

#endif // _DRAWNFEATURE_H
