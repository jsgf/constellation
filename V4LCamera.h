#ifndef _V4LCAMERA_H
#define _V4LCAMERA_H

#include "Camera.h"

class V4LCamera : public Camera
{

private:
	bool	failed_;

	int	fd_;
	int	recfd_;

	unsigned char *buf_;

	bool use_mmap_;
	unsigned mmap_size_;
	unsigned mmap_frames_;	/* max frames mmaped */
	unsigned mmap_nextframe_;	/* next frame ready for sync */
	unsigned char *frameptrs_[32]; /* VIDEO_MAX_FRAME */

public:
	V4LCamera(framesize_t size = SIF, int rate = 15);
	~V4LCamera();

	int imageSize() const;
	bool isOK() const;

	bool start();
	void stop();

	const unsigned char *getFrame();
};

#endif
