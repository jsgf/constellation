#include "Feature.h"

Feature::Feature(float x, float y, int val)
	: init_x_(x), init_y_(y),
	  age_(0), state_(New), val_(val),
	  x_(x), y_(y), prev_x_(x), prev_y_(y)
{
}

Feature::~Feature()
{
}

void Feature::update(float x, float y)
{
	age_++;

	if (age_ > Adulthood && state_ == New)
		state_ = Mature;

	prev_x_ = x_;
	prev_y_ = y_;

	x_ = x;
	y_ = y;
}
