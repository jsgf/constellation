#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#include <linux/videodev.h>

#include "Camera.h"

const Camera::sizeinfo Camera::sizeinfo_[] = {
	{ 128,  96 },		// SQSIF
	{ 160, 120 },		// QSIF
	{ 176, 144 },		// QCIF
	{ 320, 240 },		// SIF
	{ 352, 288 },		// CIF
	{ 640, 480 },		// VGA
};

Camera::Camera(size_t size, int rate)
	: size_(size), rate_(rate), fd_(-1), buf_(NULL), use_mmap_(false)
{
}

Camera::~Camera()
{
	stop();
}

void Camera::start()
{
	struct video_capability caps;
	struct video_picture pict;
	struct video_window win;

	fd_ = open("/dev/video0", O_RDONLY);

	if (fd_ == -1) {
		perror("can't open camera");
		return;		// XXX
	}

	if (ioctl(fd_, VIDIOCGCAP, &caps) == -1) {
		perror("VIDIOGCAP failed");
		return;
	}

	if (ioctl(fd_, VIDIOCGPICT, &pict) == -1) {
		perror("VIDIOGPICT failed");
		return;
	}

	pict.palette = VIDEO_PALETTE_YUV420P; // XXX add converters

	if (ioctl(fd_, VIDIOCSPICT, &pict) == -1) {
		perror("VIDIOSPICT failed");
		return;
	}

	if (ioctl(fd_, VIDIOCGWIN, &win) == -1) {
		perror("VIDIOGWIN failed");
		return;
	}

	win.width = sizeinfo_[size_].width;
	win.height = sizeinfo_[size_].height;
	win.flags &= ~0x00ff0000;
	win.flags |= rate_ << 16;

	if (ioctl(fd_, VIDIOCSWIN, &win) == -1) {
		perror("VIDIOSWIN failed");
		return;
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
}

void Camera::stop()
{
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

int Camera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}

unsigned char *Camera::getFrame()
{
	if (use_mmap_) {
		unsigned char *ptr;

		if (0)
			printf("VIDIOCSYNC frame %u\n", mmap_nextframe_);
		if (ioctl(fd_, VIDIOCSYNC, &mmap_nextframe_) == -1) {
			perror("VIDIOSYNC failed");
		}
		ptr = frameptrs_[mmap_nextframe_];
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

		return ptr;
	} else {
		read(fd_, buf_, imageSize());
		return buf_;
	}
}
