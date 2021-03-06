// -*- C++ -*-

#ifndef _1394_CAMERA_H
#define _1394_CAMERA_H

#include "Camera.h"

#include <libraw1394/raw1394.h>
#include <dc1394/control.h>


class DC1394Camera: public Camera
{
	bool	failed_;

	raw1394handle_t		camhandle_;
	dc1394_t		*dc1394;
	dc1394camera_t		*camera_;
	dc1394featureset_t	features_;

	dc1394video_mode_t	format_;
	dc1394framerate_t	fps_;

	unsigned char *buf_;

public:
	DC1394Camera(framesize_t size, int rate);
	~DC1394Camera();

	int imageSize() const;
	
	bool isOK() const;

	bool start();
	void stop();

	const unsigned char *getFrame();
};

#endif	// _1394_CAMERA_H
