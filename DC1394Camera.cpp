#include "DC1394Camera.h"

static const int MAX_PORTS = 4;
static const int DROP_FRAMES = 1;
static const int NUM_BUFFERS = 8;

DC1394Camera::DC1394Camera(framesize_t size, int rate)
	: Camera(size, rate), failed_(true),
	  camhandle_(NULL), buf_(NULL)
{
}

DC1394Camera::~DC1394Camera()
{
	stop();
}

void DC1394Camera::start()
{
	// for now, only use the first discovered camera
	struct raw1394_portinfo ports[MAX_PORTS];
	char *device_name = NULL;

	failed_ = true;
	
	raw1394handle_t raw_handle = raw1394_new_handle();

	if (raw_handle == NULL) {
		perror("Can't get raw handle");
		return;
	}

	int numPorts = raw1394_get_port_info(raw_handle, ports, MAX_PORTS);
	raw1394_destroy_handle(raw_handle);

	if (numPorts == 0) {
		fprintf(stderr, "No ieee1394 ports found\n");
		return;
	}

	for(int p = 0; p < numPorts && camhandle_ == NULL; p++) {
		int camCount;

		/* get the camera nodes and describe them as we find them */
		raw_handle = raw1394_new_handle();
		raw1394_set_port( raw_handle, p );
		nodeid_t *camera_nodes = dc1394_get_camera_nodes(raw_handle, &camCount, 1);
		raw1394_destroy_handle(raw_handle);

		for(int i = 0; i < camCount; i++) {
			unsigned channel, speed;
			
			camhandle_ = dc1394_create_handle(p);
			
			if (camhandle_ == NULL) {
				perror("can't create handle for camera");
				continue;
			}

			camera_.node = camera_nodes[i];
			if(dc1394_get_camera_feature_set(camhandle_, camera_.node, &features_) !=DC1394_SUCCESS) {
				fprintf(stderr, "Can't get feature set\n");
				dc1394_destroy_handle(camhandle_);
				camhandle_ = NULL;
				continue;
			}
			if (dc1394_get_iso_channel_and_speed(camhandle_, camera_.node,
							     &channel, &speed) != DC1394_SUCCESS) {
				fprintf(stderr, "Can't get isoc speed and channel\n");
				dc1394_destroy_handle(camhandle_);
				camhandle_ = NULL;
				continue;
			}

			switch(size_) {
			case SQSIF:
			case QSIF:
			case QCIF:
			case SIF:
			default:
				format_ = MODE_320x240_YUV422; // 320x240 4:2:2
				break;

			case CIF:
			case VGA:
				format_ = MODE_640x480_YUV411; // 640x480 YUV4:1:1
				break;
			}

			switch(rate_) {
			default:
			case 15:	fps_ = FRAMERATE_15; break;
			case 30:	fps_ = FRAMERATE_30; break;
			case 60:	fps_ = FRAMERATE_60; break;
			}

			if (dc1394_dma_setup_capture(camhandle_, camera_.node, i+1 /*channel*/,
						     FORMAT_VGA_NONCOMPRESSED, format_,
						     SPEED_400, fps_, NUM_BUFFERS, DROP_FRAMES,
						     device_name, &camera_) != DC1394_SUCCESS) {
				fprintf(stderr, "unable to setup camera- check line %d of %s to make sure\n",
					__LINE__,__FILE__);
				perror("that the video mode,framerate and format are supported\n");
				printf("is one supported by your camera\n");
				dc1394_destroy_handle(camhandle_);
				camhandle_ = NULL;
				continue;
			}
		
			/*have the camera start sending us data*/
			if (dc1394_start_iso_transmission(camhandle_, camera_.node) !=DC1394_SUCCESS) {
				perror("unable to start camera iso transmission\n");
				dc1394_destroy_handle(camhandle_);
				camhandle_ = NULL;
			}

			break;
		}
	}

	failed_ = camhandle_ == NULL;

	if (!failed_)
		buf_ = new unsigned char[imageSize()];
}

void DC1394Camera::stop()
{
	stopRecord();

	if (camhandle_ != NULL) {
		dc1394_dma_unlisten(camhandle_, &camera_);
		dc1394_dma_release_camera(camhandle_, &camera_);
		dc1394_destroy_handle(camhandle_);
	}

	camhandle_ = NULL;

	delete[] buf_;
	buf_ = NULL;
}

const unsigned char *DC1394Camera::getFrame()
{
	if (failed_)
		return testpattern();

	failed_ = dc1394_dma_single_capture(&camera_) != DC1394_SUCCESS;

	if (!failed_) {
		switch(format_) {
		case MODE_640x480_YUV411: {
			const unsigned char *in = (const unsigned char *)camera_.capture_buffer;
			unsigned char *out = buf_;

			for(int i = 0; i < 640*480/2; i++) {
				in++;
				*out++ = *in++;
				*out++ = *in++;
			}
			break;
		}
		case MODE_320x240_YUV422: {
			const unsigned char *in = (const unsigned char *)camera_.capture_buffer;
			unsigned char *out = buf_;

			for(int i = 0; i < 320*240/2; i++) {
				*in++;
				*out++ = *in++;
				*in++;
				*out++ = *in++;
			}

			break;
		}
		}
		dc1394_dma_done_with_buffer(&camera_);
		return buf_;
	} else
		return testpattern();
}

int DC1394Camera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}
