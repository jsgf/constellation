#ifndef _CAMERA_H
#define _CAMERA_H

class Camera
{
public:
	typedef enum size {
		SQSIF,		// 128x96
		QSIF,		// 160x120
		QCIF,		// 176x144
		SIF,		// 320x240
		CIF,		// 352x288
		VGA		// 640x480
	} size_t;

private:
	static const struct sizeinfo {
		int width, height;
	} sizeinfo_[];

	size_t	size_;
	int	rate_;

	int	fd_;

	unsigned char *buf_;

	bool use_mmap_;
	unsigned mmap_size_;
	unsigned mmap_frames_;	/* max frames mmaped */
	unsigned mmap_nextframe_;	/* next frame ready for sync */
	unsigned char *frameptrs_[32]; /* VIDEO_MAX_FRAME */

public:
	Camera(size_t size = SIF, int rate = 15);
	~Camera();

	void start();
	void stop();

	int imageSize() const;
	int imageWidth() const { return sizeinfo_[size_].width; }
	int imageHeight() const { return sizeinfo_[size_].height; }

	int getRate() const { return rate_; }

	unsigned char *getFrame();
};

#endif	// _CAMERA_H
