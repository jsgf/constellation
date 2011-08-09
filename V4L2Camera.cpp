#include <stdio.h>
#include <fcntl.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include "V4L2Camera.h"

V4L2Camera::V4L2Camera(Camera::framesize_t size, int rate)
	: Camera(size, rate),
	  fd_(-1), buf_(NULL), use_mmap_(true)
{
}

V4L2Camera::~V4L2Camera()
{
	stop();
}

int V4L2Camera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}

bool V4L2Camera::start()
{
	failed_ = true;

	fd_ = open("/dev/video0", O_RDONLY);
	if (fd_ == -1) {
		perror("open of /dev/video0 failed");
		return false;
	}

	struct v4l2_capability caps;

	if (ioctl(fd_, VIDIOC_QUERYCAP, &caps) == -1) {
		perror("QUERYCAP failed");
		return false;
	}

	printf("Probing %s \"%s\" (%s)\n",
	       caps.bus_info, caps.card, caps.driver);

	if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		printf("Device not capable of video capture\n");
		return false;
	}

	if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
		printf("No streaming\n");
		use_mmap_ = false;
	}

	if (!(caps.capabilities & V4L2_CAP_READWRITE)) {
		printf("No read/write");
		if (!use_mmap_) {
			printf("... and no mmap!\n");
			return false;
		}
		printf("\n");
	}

	struct v4l2_format format;
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(fd_, VIDIOC_G_FMT, &format) == -1) {
		perror("G_FMT failed");
		return false;
	}

	printf("pixelformat=%x width=%d height=%d\n",
	       format.fmt.pix.pixelformat, format.fmt.pix.width,
	       format.fmt.pix.height);

	unsigned long want = 0;

	printf("Formats:\n");
	for (int i = 0;; i++) {
		struct v4l2_fmtdesc desc;

		desc.index = i;
		desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (ioctl(fd_, VIDIOC_ENUM_FMT, &desc) == -1)
			break;

		printf("   %d: %s%s\n", i, desc.description, desc.flags &
		       V4L2_FMT_FLAG_COMPRESSED ? " (compressed)" : "" );

		if (want == 0 && !(desc.flags & V4L2_FMT_FLAG_COMPRESSED))
			want = desc.pixelformat;

		for (int j = 0;; j++) {
			struct v4l2_frmsizeenum framesz;

			framesz.index = j;
			framesz.pixel_format = desc.pixelformat;
			framesz.type = V4L2_FRMSIZE_TYPE_DISCRETE;

			if (ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &framesz) == -1)
				break;

			printf("      %dx%d%s\n",
			       framesz.discrete.width, framesz.discrete.height,
			       format.fmt.pix.pixelformat == desc.pixelformat &&
			       format.fmt.pix.width == framesz.discrete.width &&
			       format.fmt.pix.height == framesz.discrete.height ? " *" : "");
		}
	}

	if (want == 0) {
		printf("Didn't find any pixel formats we want\n");
		return false;
	}

	pixfmt_ = want;
	format.fmt.pix.pixelformat = want;
	format.fmt.pix.width = sizeinfo_[size_].width;
	format.fmt.pix.height = sizeinfo_[size_].height;

	printf("set to %dx%d\n", sizeinfo_[size_].width, sizeinfo_[size_].height);

	if (ioctl(fd_, VIDIOC_S_FMT, &format) == -1) {
		perror("S_FMT failed");
		return false;
	}

	if (use_mmap_) {
		struct v4l2_requestbuffers reqbuf = {};

		reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		reqbuf.memory = V4L2_MEMORY_MMAP;
		reqbuf.count = max_buffers_;

		if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) == -1) {
			perror("REQBUFS failed");
			use_mmap_ = false;
			goto fail_reqbuf;
		}

		printf("got %d buffers\n", reqbuf.count);
		mmap_frames_ = reqbuf.count;

		for (unsigned i = 0; i < reqbuf.count; i++) {
			struct v4l2_buffer buffer = {};

			buffer.type = reqbuf.type;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = i;

			if (ioctl(fd_, VIDIOC_QUERYBUF, &buffer) == -1) {
				perror("QUERYBUF failed");
				goto fail_reqbuf;
			}

			frame_size_ = buffer.length;
			frameptrs_[i] = (unsigned char *)mmap(NULL, buffer.length,
							      PROT_READ, MAP_SHARED,
							      fd_, buffer.m.offset);
			if (frameptrs_[i] == MAP_FAILED) {
				perror("mmap failed");
				goto fail_reqbuf;
			}

			if (ioctl(fd_, VIDIOC_QBUF, &buffer) == -1) {
				perror("initial QBUF");
				goto fail_reqbuf;
			}
		}

		mmap_prev_frame_ = ~0;

		int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(fd_, VIDIOC_STREAMON, &type) == -1) {
			perror("STREAMON failed");
			goto fail_reqbuf;
		}
	} else {
	fail_reqbuf:
		use_mmap_ = false;

		// XXX get proper bytes/pix for format
		frame_size_ = sizeinfo_[size_].width * sizeinfo_[size_].height * 2;
	}

	failed_ = false;

	return isOK();
}

bool V4L2Camera::isOK() const
{
	return !failed_;
}

void V4L2Camera::stop()
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(fd_, VIDIOC_STREAMOFF, &type);

	stopRecord();

	if (fd_ != -1) {
		close(fd_);
		fd_ = -1;
	}

	if (use_mmap_) {
		for (unsigned i = 0; i < mmap_frames_; i++) {
			if (frameptrs_[i]) {
				munmap(frameptrs_[i], frame_size_);
				frameptrs_[i] = NULL;
			}
		}
	}

	if (retbuf_) {
		delete retbuf_;
		retbuf_ = NULL;
	}
}

static bool needsconv(unsigned long pixfmt)
{
	switch (pixfmt) {
	default:
	case V4L2_PIX_FMT_YUV420:
		return false;

	case V4L2_PIX_FMT_YUYV:
		return true;
	}
}

static void convert(unsigned long pixfmt, size_t inbytes,
		    const unsigned char *in, unsigned char *out)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_YUYV:
		for (; inbytes > 4; inbytes -= 4) {
			out[0] = in[0];
			out[1] = in[2];

			out += 2;
			in += 4;
		}
		break;

	default:
		memcpy(out, in, inbytes);
	}
}

const unsigned char *V4L2Camera::getFrame()
{
	const unsigned char *outbuf;
	const unsigned char *inbuf;
	unsigned char *tmpbuf = NULL;

	if (!isOK()) {
	failed:
		failed_ = true;
		return testpattern();
	}

	if (use_mmap_) {
		struct v4l2_buffer buffer = {};

		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;

		if (mmap_prev_frame_ != ~0) {
			buffer.index = mmap_prev_frame_;

			if (ioctl(fd_, VIDIOC_QBUF, &buffer) == -1) {
				perror("QBUF failed");
				goto failed;
			}
		}

		if (ioctl(fd_, VIDIOC_DQBUF, &buffer) == -1) {
			perror("DQBUF failed");
			goto failed;
		}

		printf("got index %d frame %u\n",
		       buffer.index, buffer.sequence);
		inbuf = frameptrs_[buffer.index];
		mmap_prev_frame_ = buffer.index;
	} else {
		tmpbuf = new unsigned char[frame_size_];
		read(fd_, tmpbuf, frame_size_);

		inbuf = tmpbuf;
	}

	if (needsconv(pixfmt_)) {
		if (retbuf_ == NULL)
			retbuf_ = new unsigned char[frame_size_];

		outbuf = retbuf_;
		convert(pixfmt_, frame_size_, inbuf, retbuf_);
	} else {
		outbuf = inbuf;
	}

	if (tmpbuf)
		delete tmpbuf;

	return outbuf;
}

