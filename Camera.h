#ifndef _CAMERA_H
#define _CAMERA_H

#define HAVE_STDINT_H
#include <yuv4mpeg.h>

class Camera
{
  public:
	typedef enum {
		SQSIF,		// 128x96
		QSIF,		// 160x120
		QCIF,		// 176x144
		SIF,		// 320x240
		CIF,		// 352x288
		VGA,		// 640x480

		FRAMESIZE_MAX
	} framesize_t;

	static const struct sizeinfo {
		int width, height;
	} sizeinfo_[];

  protected:
	framesize_t	size_;
	int		rate_;

	const unsigned char *testpattern();

	/* stream recording */
	y4m_stream_info_t stream_;
	int		recfd_;

	void writeRecordHeader();
	void writeFrame(const unsigned char *data);

  public:
	Camera(framesize_t size, int rate);
	virtual ~Camera();

	virtual int imageSize() const;
	int imageWidth() const { return sizeinfo_[size_].width; }
	int imageHeight() const { return sizeinfo_[size_].height; }

	int getRate() const { return rate_; }

	virtual const unsigned char *getFrame() = 0;

	virtual bool start() = 0;
	virtual void stop() = 0;

	virtual bool isOK() const = 0;

	void startRecord(int fd);
	void stopRecord();

	bool isrecording() const { return recfd_ != -1; }
};

class FileCamera : public Camera
{
	const char *file_;
	int fd_;
	
	off_t filesize_;
	unsigned char *buf_;
	unsigned char *bufptr_;

  public:
	FileCamera(const char *filename);

	int imageSize() const;

	bool isOK() const;

	bool start();
	void stop();

	const unsigned char *getFrame();
};

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

#endif	// _CAMERA_H
