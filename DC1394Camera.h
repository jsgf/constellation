// -*- C++ -*-

#ifndef _1394_CAMERA_H
#define _1394_CAMERA_H

#include "Camera.h"

#include <libraw1394/raw1394.h>
#include <libdc1394/dc1394_control.h>


class DC1394Camera: public Camera
{
	bool	failed_;

	raw1394handle_t		camhandle_;
	dc1394_cameracapture	camera_;
	dc1394_feature_set	features_;

	int format_;
	int fps_;

	unsigned char *buf_;

public:
	DC1394Camera(framesize_t size, int rate);
	~DC1394Camera();

	int imageSize() const;
	
	void start();
	void stop();

	const unsigned char *getFrame();
};

#endif	// _1394_CAMERA_H
