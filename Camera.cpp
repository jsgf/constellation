#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
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

Camera::Camera(Camera::framesize_t size, int rate)
	: size_(size), rate_(rate), recfd_(-1)
{
}

Camera::~Camera()
{
	stopRecord();
}

int Camera::imageSize() const
{
	return sizeinfo_[size_].width * sizeinfo_[size_].height;
}

void Camera::startRecord(int fd)
{
	stopRecord();

	recfd_ = fd;

	writeRecordHeader();
}

void Camera::stopRecord()
{
	if (recfd_ != -1) {
		y4m_fini_stream_info(&stream_);
		close(recfd_);
		recfd_ = -1;
	}
}

void Camera::writeRecordHeader()
{
	y4m_ratio_t framerate = { rate_, 1 };
	y4m_ratio_t oneone = { 1, 1 };

	y4m_init_stream_info(&stream_);

	y4m_si_set_width (&stream_, sizeinfo_[size_].width);
	y4m_si_set_height(&stream_, sizeinfo_[size_].height);
	y4m_si_set_interlace(&stream_, Y4M_ILACE_NONE);
	y4m_si_set_framerate(&stream_, framerate);
	y4m_si_set_sampleaspect(&stream_, oneone);
	
	y4m_write_stream_header(recfd_, &stream_);
}

void Camera::writeFrame(const unsigned char *data)
{
	y4m_frame_info_t fi;

	y4m_init_frame_info(&fi);

	int ysz = sizeinfo_[size_].width * sizeinfo_[size_].height;
	int uvsz = ysz / 4;

	uint8_t *yuv[3];
	yuv[0] = (uint8_t *)data;
	yuv[1] = (uint8_t *)data + ysz;
	yuv[2] = (uint8_t *)data + ysz + uvsz;
	y4m_write_frame(recfd_, &stream_, &fi, yuv);

	y4m_fini_frame_info(&fi);
}

const unsigned char *Camera::testpattern()
{
	extern unsigned char nbc_320[];
	extern unsigned char Indian_Head_320[];
	extern unsigned char tcf_sydney[];
	static unsigned char *tests[] = {
		nbc_320, Indian_Head_320, tcf_sydney,
	};
	stop();

	size_ = SIF;
	static unsigned char *tp = NULL;

	stop();		// disable any use of camera

	size_ = SIF;	// caller probably isn't expecting this

	if (tp == NULL || (random() < (RAND_MAX / 1000)))
		tp = tests[random() % (sizeof(tests)/sizeof(*tests))];

	return tp;
}



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


FileCamera::FileCamera(const char *file)
	: Camera(QSIF, 10), 
	  file_(file), fd_(-1)
{
}

bool FileCamera::start()
{
	fd_ = open(file_, O_RDONLY);

	if (fd_ == -1) {
		perror("FileCamera::start open");
		return false;
	}

	y4m_init_stream_info(&stream_);
	int err = y4m_read_stream_header(fd_, &stream_);

	if (err != Y4M_OK) {
		printf("y4m_read_stream_header failed: %s\n", y4m_strerr(err));
		stop();
		return false;
	}

	y4m_ratio_t framerate = y4m_si_get_framerate(&stream_);
	rate_ = framerate.n / framerate.d;

	int i;
	for(i = 0; i < FRAMESIZE_MAX; i++) {
		if (sizeinfo_[i].width == y4m_si_get_width(&stream_) &&
		    sizeinfo_[i].height == y4m_si_get_height(&stream_))
			break;
	}

	if (i == FRAMESIZE_MAX) {
		stop();
		return false;
	}
	size_ = (framesize_t)i;

	printf("y2m_si_get_framelength=%d imageSize()=%d\n",
	       y4m_si_get_framelength(&stream_), imageSize());

	buf_ = new unsigned char[y4m_si_get_framelength(&stream_)];

	return isOK();
}

void FileCamera::stop()
{
	if (fd_ != -1) {
		close(fd_);
		fd_ = -1;
		delete[] buf_;
		buf_ = NULL;
	}

	y4m_fini_stream_info(&stream_);
}

int FileCamera::imageSize() const
{
	// include Y and UV planes
	return sizeinfo_[size_].width * (sizeinfo_[size_].height * 3 / 2);
}

bool FileCamera::isOK() const
{
	return buf_ != NULL && fd_ != -1;
}

const unsigned char *FileCamera::getFrame()
{
	if (!isOK())
		return testpattern();

	y4m_frame_info_t fi;

	y4m_init_frame_info(&fi);

	int ysz = sizeinfo_[size_].width * sizeinfo_[size_].height;
	int uvsz = ysz / 4;

	uint8_t *yuv[3];
	yuv[0] = (uint8_t *)buf_;
	yuv[1] = (uint8_t *)buf_ + ysz;
	yuv[2] = (uint8_t *)buf_ + ysz + uvsz;
	int err = y4m_read_frame(fd_, &stream_, &fi, yuv);

	if (err != Y4M_OK) {
		printf("y4m_read_frame failed: %s\n", y4m_strerr(err));
		stop();
		return testpattern();
	}

	y4m_fini_frame_info(&fi);

	return buf_;
}
