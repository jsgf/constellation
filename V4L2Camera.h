#ifndef _V4L2CAMERA_H
#define _V4L2CAMERA_H

#include "Camera.h"

class V4L2Camera : public Camera
{

private:
	bool	failed_;

	int	fd_;
	int	recfd_;

	static const int max_buffers_ = 10;

	unsigned frame_size_;	/* total size of frame */

	bool use_mmap_;

	unsigned mmap_frames_;	/* max frames mmaped */
	unsigned mmap_prev_frame_;	/* next frame ready for sync */
	unsigned char *frameptrs_[max_buffers_];

	unsigned long pixfmt_;	/* raw pixel format */

	unsigned char *retbuf_;	/* buffer used to return if raw isn't useful */

public:
	V4L2Camera(framesize_t size = SIF, int rate = 15);
	~V4L2Camera();

	int imageSize() const;
	bool isOK() const;

	bool start();
	void stop();

	const unsigned char *getFrame();
};

#endif
