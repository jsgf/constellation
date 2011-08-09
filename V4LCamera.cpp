#include <linux/videodev.h>

#include "V4LCamera.h"

V4LCamera::V4LCamera(Camera::framesize_t size, int rate)
	: Camera(size, rate),
	  fd_(-1), buf_(NULL), use_mmap_(false)
{
}

V4LCamera::~V4LCamera()
{
	stop();
}

int V4LCamera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}

bool V4LCamera::start()
{
	struct video_capability caps;
	struct video_picture pict;
	struct video_window win;

	failed_ = true;

	fd_ = open("/dev/video0", O_RDONLY);

	if (fd_ == -1) {
		perror("can't open camera");
		return false;
	}

	if (ioctl(fd_, VIDIOCGCAP, &caps) == -1) {
		perror("VIDIOGCAP failed");
		return false;
	}

	if (ioctl(fd_, VIDIOCGPICT, &pict) == -1) {
		perror("VIDIOGPICT failed");
		return false;
	}

	pict.palette = VIDEO_PALETTE_YUV420P; // XXX add converters

	if (ioctl(fd_, VIDIOCSPICT, &pict) == -1) {
		perror("VIDIOSPICT failed");
		return false;
	}

	if (ioctl(fd_, VIDIOCGWIN, &win) == -1) {
		perror("VIDIOGWIN failed");
		return false;
	}

	win.width = sizeinfo_[size_].width;
	win.height = sizeinfo_[size_].height;
	win.flags &= ~0x00ff0000;
	win.flags |= rate_ << 16;

	if (ioctl(fd_, VIDIOCSWIN, &win) == -1) {
		perror("VIDIOSWIN failed");
		return false;
	}

	struct video_mbuf vidmbuf;
	if (ioctl(fd_, VIDIOCGMBUF, &vidmbuf) != -1) {
		printf("video mbufs: %d frames, %d bytes\n",
		       vidmbuf.frames, vidmbuf.size);

		mmap_frames_ = vidmbuf.frames;
		mmap_nextframe_ = 0;
		mmap_size_ = vidmbuf.size;
		buf_ = (unsigned char *)mmap(0, mmap_size_, PROT_READ, MAP_SHARED, fd_, 0);
		for(unsigned i = 0; i < mmap_frames_; i++) {
			printf("  offset %u = %d\n", i, vidmbuf.offsets[i]);
			frameptrs_[i] = buf_ + vidmbuf.offsets[i];
		}

		struct video_mmap vidmmap;
		vidmmap.frame = mmap_nextframe_;
		vidmmap.height = imageHeight();
		vidmmap.width = imageWidth();
		vidmmap.format = VIDEO_PALETTE_YUV420P;

		printf("vidmmap.frame=%u height=%d width=%d format=%u\n",
		       vidmmap.frame, vidmmap.height, vidmmap.width, vidmmap.format);
		if (ioctl(fd_, VIDIOCMCAPTURE, &vidmmap) == 0) {
			printf("using mmap interface\n");
			use_mmap_ = true;
		}
	} else
		buf_ = new unsigned char[imageSize()];

	failed_ = false;

	return isOK();
}

void V4LCamera::stop()
{
	stopRecord();

	if (fd_ != -1) {
		close(fd_);
		fd_ = -1;
	}

	if (buf_ != NULL) {
		if (use_mmap_)
			munmap(buf_, mmap_size_);
		else
			delete[] buf_;

		buf_ = NULL;
	}
}

bool V4LCamera::isOK() const
{
	return !failed_;
}

const unsigned char *V4LCamera::getFrame()
{
	unsigned char *ret;

	if (!isOK())
		return testpattern();

	if (use_mmap_) {
		if (0)
			printf("VIDIOCSYNC frame %u\n", mmap_nextframe_);
		if (ioctl(fd_, VIDIOCSYNC, &mmap_nextframe_) == -1) {
			failed_ = true;
			perror("VIDIOSYNC failed");
		}
		ret = frameptrs_[mmap_nextframe_];
		mmap_nextframe_ = (mmap_nextframe_ + 1) % mmap_frames_;

		struct video_mmap vidmmap;
		vidmmap.frame = mmap_nextframe_;
		vidmmap.height = imageHeight();
		vidmmap.width = imageWidth();
		vidmmap.format = VIDEO_PALETTE_YUV420P;

		if (0)
			printf("vidmmap.frame=%u height=%d width=%d format=%u\n",
			       vidmmap.frame, vidmmap.height, vidmmap.width, vidmmap.format);
		if (ioctl(fd_, VIDIOCMCAPTURE, &vidmmap) == -1) {
			perror("VIDIOCMCAPTURE failed - reverting to read");
			use_mmap_ = false;
			buf_ = new unsigned char[imageSize()];
		}
	} else {
		int r = read(fd_, buf_, imageSize());

		if (r != imageSize())
			failed_ = true;
		ret = buf_;
	}

	if (recfd_ != -1)
		writeFrame(ret);

	return ret;
}


