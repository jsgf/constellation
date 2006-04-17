// -*- C++ -*-

#ifndef _FEATURESET_H
#define _FEATURESET_H

#include "Feature.h"

#include <vector>

extern "C" {
#include <klt.h>
}

class FeatureSet_Base
{
public:
	typedef std::vector<Feature *> FeatureVec_t;

private:
	KLT_TrackingContextRec *klt_tc_;
	KLT_FeatureListRec *klt_fl_;

	int maxFeatures_, minFeatures_;
	int active_;

	void sync();

protected:
	FeatureVec_t	features_;

	virtual FeatureVec_t::iterator begin() { return features_.begin(); }
	virtual FeatureVec_t::iterator end() { return features_.end(); }

	virtual FeatureVec_t::const_iterator begin() const { return features_.begin(); }
	virtual FeatureVec_t::const_iterator end() const { return features_.end(); }

	virtual Feature *newFeature(float x, float y, int val) = 0;
	virtual void removeFeature(Feature *) = 0;

public:
	FeatureSet_Base(int maxFeatures, int minFeatures);
	virtual ~FeatureSet_Base();

	void setNumFeatures(int min, int max);

	virtual void update(const unsigned char *img, int width, int height);

	int borderInset_x() const { return klt_tc_->borderx; }
	int borderInset_y() const { return klt_tc_->bordery; }

	int windowWidth() const { return klt_tc_->window_width; }
	int windowHeight() const { return klt_tc_->window_height; }

	int nFeatures() const { return active_; }
};

template <class FT_ = Feature>
class FeatureSet: public FeatureSet_Base
{
protected:
	virtual Feature *newFeature(float x, float y, int val) { return new FT_(x, y, val); }
	virtual void removeFeature(Feature *f) { delete f;  }

public:
	FeatureSet(int maxFeatures, int minFeatures)
		: FeatureSet_Base(maxFeatures, minFeatures) {}
	virtual ~FeatureSet() {}

};

#endif	/* _FEATURESET_H */
