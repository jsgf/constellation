#CXX=g++ -ggdb
#LIBKLT=-lklt
CXX=icc -wd1418,981,810,279,530,383,191,1469 # -ip -xB
LIBKLT=-lklt-icc

#PROF=-pg

KLT=/home/jeremy/src/klt/klt
CGAL=/home/jeremy/src/CGAL-3.0.1
CGALPLAT=i686_Linux-2.6.7-mm7_g++-3.3.3

FREETYPE_CFLAGS := $(shell freetype-config --cflags)
FREETYPE_LIBS := $(shell freetype-config --libs)
MJPEGTOOLS_CFLAGS := $(shell mjpegtools-config --cflags)
MJPEGTOOLS_LIBS := $(shell mjpegtools-config --libs)

CXXFLAGS=-Wall -g -O -fno-inline $(PROF) \
	-I$(KLT) \
	-I$(CGAL)/include -I$(CGAL)/include/CGAL/config/$(CGALPLAT) \
	$(FREETYPE_CFLAGS) $(MJPEGTOOLS_CFLAGS)

all: constellation

TESTPAT=tcf_sydney.o Indian_Head_320.o nbc-320.o

constellation: \
	main.o Camera.o DC1394Camera.o \
	FeatureSet.o Feature.o VaultOfHeaven.o misc.o \
	star.o $(TESTPAT) # Geom.o
	$(CXX)  $(PROF) -o $@ $^ -lSDL $(FREETYPE_LIBS) $(MJPEGTOOLS_LIBS) -lGLU -lGL -L$(KLT) $(LIBKLT) -L$(CGAL)/lib/$(CGALPLAT) -Wl,-rpath,$(CGAL)/lib/$(CGALPLAT) -lCGAL -lfftw3 -lz -ldc1394_control -lraw1394

%.mpg: %.ppm.gz
	zcat $< | ppmtoy4m | mpeg2enc -f 2 -q8 -o $@

.deps:
	mkdir .deps

star.raw: star.png
	convert star.png -resize 64x64 gray:star.raw

%.raw: %.jpg
	convert -resize 320x240 $< gray:$@

%.o: %.raw
	(sym=`echo $* | tr '-' '_'`; \
	 echo -e ".data\n.global $$sym\n$$sym:\n\t.incbin \"$<\"" | as -o $@)

testpat.o: nbc-320.raw Indian_Head_320.raw tcf_sydney.raw

%.o: %.cpp .deps
	$(CXX) -c -o $@ -MD -MF .deps/$*.d $(CXXFLAGS) $<


clean:
	rm -f *.o .deps/*.d constellation

-include .deps/*.d
