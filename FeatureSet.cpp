#include "FeatureSet.h"

#include <cassert>

extern "C" {
#include <klt.h>
}

static const int HIGH_TURNOVER = 5;
static const int HOLDOFF = 10;

FeatureSet_Base::FeatureSet_Base(int maxFeatures, int minFeatures)
{
	klt_tc_ = KLTCreateTrackingContext();
	KLTSetVerbosity(0);

	klt_tc_->sequentialMode = true;
	klt_tc_->mindist = 25;
	
	setNumFeatures(minFeatures, maxFeatures);
}

FeatureSet_Base::~FeatureSet_Base()
{
	KLTFreeFeatureList(klt_fl_);
	KLTFreeTrackingContext(klt_tc_);
}

void FeatureSet_Base::setNumFeatures(int min, int max)
{
	if (min > max)
		min = max;

	for(int i = 0; i < maxFeatures_; i++)
		if (features_[i] != NULL)
			removeFeature(features_[i]);

	if (klt_fl_ == NULL)
		KLTFreeFeatureList(klt_fl_);
	klt_fl_ = KLTCreateFeatureList(max);

	minFeatures_ = min;
	maxFeatures_ = max;

	active_ = 0;

	features_.resize(max);
}

void FeatureSet_Base::sync()
{
	// Sync KLT feature state with our feature state
	for(int i = 0; i < klt_fl_->nFeatures; i++) {
		KLT_Feature f = klt_fl_->feature[i];

		if (0)
			printf("f=%p f->val=%d (%g,%g) features_[%d]=%p\n",
			       f, f->val, f->x, f->y, i, features_[i]);

		if ((f->val < 0) && (features_[i] == NULL)) {
			// do nothing
		} else if ((f->val < 0) && (features_[i] != NULL)) {
			removeFeature(features_[i]);
			features_[i] = NULL;
			active_--;
		} else if ((f->val >= 0) && (features_[i] == NULL)) {
			Feature *feature = newFeature(f->x, f->y, f->val);
			features_[i] = feature;
			active_++;
		} else if ((f->val >= 0) && (features_[i] != NULL)) {
			features_[i]->update(f->x, f->y);
		} else
			abort();
	}
}

void FeatureSet_Base::update(const unsigned char *img, int w, int h)
{
	KLT_PixelType *pix = (KLT_PixelType *)img;

	if (0)
		printf("active=%d min=%d max=%d\n",
		       active_, minFeatures_, maxFeatures_);

	if (active_ == 0) {
		KLTSelectGoodFeatures(klt_tc_, pix, w, h, klt_fl_);
		sync();
	} else {
		KLTTrackFeatures(klt_tc_, pix, pix, w, h, klt_fl_);
		sync();

		if (active_ < minFeatures_) {
			KLTReplaceLostFeatures(klt_tc_, pix, w, h, klt_fl_);
			sync();
		}
	}



	assert(active_ == KLTCountRemainingFeatures(klt_fl_));
}

template FeatureSet<Feature>;
