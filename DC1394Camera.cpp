#include "DC1394Camera.h"

static const int MAX_PORTS = 4;
static const int DROP_FRAMES = 1;
static const int NUM_BUFFERS = 8;

static const Camera::framesize_t map[] = {
	Camera::SIF, Camera::SIF, Camera::SIF, Camera::SIF,
	Camera::VGA, Camera::VGA,
};

DC1394Camera::DC1394Camera(framesize_t size, int rate)
	: Camera(map[size], rate), failed_(true),
	  camera_(NULL), buf_(NULL)
{
}

DC1394Camera::~DC1394Camera()
{
	stop();
}

bool DC1394Camera::start()
{
	dc1394camera_t **cameras = NULL;
	unsigned ncameras;
	char *device_name = NULL;
	dc1394speed_t speed;

	failed_ = true;

	int err = dc1394_find_cameras(&cameras, &ncameras);

	if (err != DC1394_SUCCESS) {
		printf("can't find cameras\n");
		return false;
	}

	if (ncameras < 1) {
		printf("no cameras found\n");
		return false;
	}

	camera_ = cameras[0];
	for(unsigned i = 1; i < ncameras; i++)
		dc1394_free_camera(cameras[i]);
	free(cameras);

	if (dc1394_get_camera_feature_set(camera_, &features_) != DC1394_SUCCESS) {
		printf("unable to get feature set\n");
		return false;
	}
	dc1394_print_feature_set(&features_);

	if (dc1394_video_get_iso_speed(camera_, &speed) != DC1394_SUCCESS) {
		printf("can't get speed\n");
		return false;
	}


	switch(size_) {
	case SQSIF:
	case QSIF:
	case QCIF:
	case SIF:
	default:
		size_ = SIF;
		format_ = DC1394_VIDEO_MODE_320x240_YUV422; // 320x240 4:2:2
		break;

	case CIF:
	case VGA:
		size_ = VGA;
		format_ = DC1394_VIDEO_MODE_640x480_YUV411; // 640x480 YUV4:1:1
		break;
	}

	switch(rate_) {
	default:
	case 15:	fps_ = DC1394_FRAMERATE_15; break;
	case 30:	fps_ = DC1394_FRAMERATE_30; break;
	case 60:	fps_ = DC1394_FRAMERATE_60; break;
	}

	if (device_name != NULL && 
	    dc1394_set_dma_device_filename(camera_, device_name) != DC1394_SUCCESS) {
		printf("can't set device name\n");
		return false;
	}

	if (dc1394_dma_setup_capture(camera_,
				     format_, speed, fps_, 
				     NUM_BUFFERS, DROP_FRAMES) != DC1394_SUCCESS) {
		fprintf(stderr, "unable to setup camera- check line %d of %s to make sure\n",
			__LINE__,__FILE__);
		perror("that the video mode,framerate and format are supported\n");
		printf("is one supported by your camera\n");
		return false;
	}
		
	/*have the camera start sending us data*/
	if (dc1394_video_set_transmission(camera_, DC1394_ON) != DC1394_SUCCESS) {
		perror("unable to start camera iso transmission\n");
		return false;
	}

	failed_ = false;

	if (!failed_)
		buf_ = new unsigned char[imageSize()];

	return isOK();
}

void DC1394Camera::stop()
{
	stopRecord();

	if (camera_ != NULL) {
		dc1394_dma_unlisten(camera_);
		dc1394_dma_release_camera(camera_);
		dc1394_free_camera(camera_);
		camera_ = NULL;
	}

	delete[] buf_;
	buf_ = NULL;
}

bool DC1394Camera::isOK() const
{
	return !failed_;
}

const unsigned char *DC1394Camera::getFrame()
{
	if (!isOK())
		return testpattern();

	failed_ = dc1394_dma_capture(&camera_, 1, DC1394_VIDEO1394_WAIT) != DC1394_SUCCESS;

	if (!failed_) {
		switch(format_) {
		case DC1394_VIDEO_MODE_640x480_YUV411: {
			const unsigned char *in = (const unsigned char *)camera_->capture.capture_buffer;
			unsigned char *out = buf_;

			for(int i = 0; i < 640*480/2; i++) {
				in++;
				*out++ = *in++;
				*out++ = *in++;
			}
			break;
		}
		case DC1394_VIDEO_MODE_320x240_YUV422: {
			const unsigned char *in = (const unsigned char *)camera_->capture.capture_buffer;
			unsigned char *out = buf_;

			for(int i = 0; i < 320*240/2; i++) {
				*in++;
				*out++ = *in++;
				*in++;
				*out++ = *in++;
			}

			break;
		}

		default:
			abort(); // never used
		}
		dc1394_dma_done_with_buffer(camera_);


		if (recfd_ != -1)
			writeFrame(buf_);

		return buf_;
	} else
		return testpattern();
}

int DC1394Camera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}
