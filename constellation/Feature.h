#ifndef _FEATURE_H
#define _FEATURE_H

extern "C" {
#include <klt.h>
}

class Feature
{
public:
	static const int Adulthood = 10;

	typedef enum state {
		Dead,
		New,
		Mature,
		Floating
	} state_t;

	const float init_x_, init_y_;
protected:
	int	age_;
	state_t	state_;
	int	val_;

	float x_, y_;
	float prev_x_, prev_y_;
public:
	Feature(float x, float y, int val);
	virtual ~Feature();

	state_t getState() const { return state_; }

	bool isTracked() const { return state_ != Dead && state_ != Floating; }

	virtual void update(float x, float y);

	float deltaX() const { return x_ - prev_x_; }
	float deltaY() const { return y_ - prev_y_; }

	float x() const { return x_; }
	float y() const { return y_; }

	float init_x() const { return init_x_; }
	float init_y() const { return init_x_; }

	int val() const { return val_; }
};

#endif	/* _FEATURE_H */
