COMPILER:=gcc

gcc_CXX:=g++
gcc_CC:=gcc

pcc_CXX:=/opt/pathscale/bin/pathCC-2.0 -fmangle-conv-op-name  
pcc_CC:=/opt/pathscale/bin/pathcc-2.0  

icc_CXX:=icpc
icc_CC:=icc

# PROF:=-pg

#CGAL:=/home/jeremy/robot/src/CGAL-3.0.1
#CGALPLAT:=i686_Linux-2.6.7-rc3-mm2_g++-3.3.3
CGAL:=/home/jeremy/robot/src/CGAL-3.1
CGALPLAT:=i686_Linux-2.6.11-mm4_g++-3.4.2

KLTSRC:=convolve.c error.c pnmio.c pyramid.c selectGoodFeatures.c \
	storeFeatures.c trackFeatures.c klt.c klt_util.c writeFeatures.c
KLTOBJ:=$(addprefix klt/,$(KLTSRC:%.c=%.o))

FREETYPE_CFLAGS := $(shell freetype-config --cflags)
FREETYPE_LIBS := $(shell freetype-config --libs)
MJPEGTOOLS_CFLAGS := $(shell mjpegtools-config --cflags)
MJPEGTOOLS_LIBS := $(shell mjpegtools-config --libs)
SDL_CFLAGS := $(shell sdl-config --cflags)
SDL_LIBS := $(shell sdl-config --libs)
LUA_CFLAGS :=
LUA_LIBS := -llua -llualib

gcc_FLAGS:=-Wall
icc_FLAGS:= -wd1418,981,810,279,530,383,191,1469

gcc_DEBUG:=-ggdb -fno-inline -O
gcc_OPT:=-ggdb -O2 -msse -mfpmath=sse -march=pentium-m

pcc_OPT:=-O3 -LNO:simd_verbose=ON -LNO:simd=2 -LNO:prefetch=2 #-fb_opt fbdata #  -g -fno-inline
#pcc_OPT:=-Ofast -LNO:prefetch=2

icc_OPT:=-O3 -xN

CC:=$($(COMPILER)_CC)
CXX:=$($(COMPILER)_CXX)

OPT:=$($(COMPILER)_OPT)

CPPFLAGS:= -I/usr/local/include \
	-Iklt \
	-I$(CGAL)/include -I$(CGAL)/include/CGAL/config/$(CGALPLAT) \
	$(SDL_CFLAGS) \
	$(FREETYPE_CFLAGS) \
	$(MJPEGTOOLS_CFLAGS) \
	$($(COMPILER)_CPPFLAGS)

CXXFLAGS:=$(OPT) $(PROF) $(CPPFLAGS) $($(COMPILER)_FLAGS) $($(COMPILER)_CXXFLAGS)
CFLAGS:=$(OPT) $(CPPFLAGS)  $($(COMPILER)_FLAGS) $($(COMPILER)_CFLAGS)

LDFLAGS:=$($(COMPILER)_LDFLAGS)
LIBS:= \
	$(SDL_LIBS) \
	$(FREETYPE_LIBS) \
	$(MJPEGTOOLS_LIBS) \
	$(LUA_LIBS) \
	-lGLU -lGL \
	-L$(CGAL)/lib/$(CGALPLAT) -Wl,-rpath,$(CGAL)/lib/$(CGALPLAT) -lCGAL \
	-lfftw3 \
	-lz \
	-ldc1394_control -lraw1394 \
	-lm

BOKLIBS = \
	$(SDL_LIBS) \
	$(LUA_LIBS) \
	$(MJPEGTOOLS_LIBS) \
	-lGLU -lGL -lz -ldc1394_control -lraw1394 -lm

all: constellation bokchoi

TESTPAT=tcf_sydney.o Indian_Head_320.o nbc-320.o

constellation: \
	main.o Camera.o DC1394Camera.o \
	FeatureSet.o Feature.o VaultOfHeaven.o misc.o \
	star.o $(TESTPAT) $(KLTOBJ) # Geom.o
	$(CXX)  $(PROF) $(OPT) $(LDFLAGS) -o $@ $^ $(filter-out -L/usr/lib64,$(LIBS))

bokchoi: bokchoi.o bok_lua.o \
	Camera.o DC1394Camera.o \
	$(KLTOBJ) $(TESTPAT)
	$(CXX)  $(PROF) $(OPT) $(LDFLAGS) -o $@ $^ \
		$(filter-out -L/usr/lib64,$(BOKLIBS))

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

%.o: %.c .deps
	$(CC) -c -o $@ -MD -MF .deps/$(subst /,-,$*).d $(CFLAGS) $<


clean:
	rm -f *.o klt/*.o .deps/*.d constellation

-include .deps/*.d
