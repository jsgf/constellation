CXX=g++ -ggdb
LIBKLT=-lklt
#CXX=icc -wd1418,981,810,279,530,383 # -ip -xB
#LIBKLT=-lklt-icc

#PROF=-pg

KLT=/home/jeremy/robot/src/klt
CGAL=/home/jeremy/robot/src/CGAL-3.0.1
CGALPLAT=i686_Linux-2.6.7-rc3-mm2_g++-3.3.3

CXXFLAGS=-Wall -g -O -fno-inline $(PROF) -I$(KLT) -I$(CGAL)/include -I$(CGAL)/include/CGAL/config/$(CGALPLAT)

all: constellation star.raw

constellation: main.o Camera.o FeatureSet.o Feature.o VaultOfHeaven.o misc.o # Geom.o
	$(CXX)  $(PROF) -o $@ $^ -lglut -lGL -L$(KLT) $(LIBKLT) -L$(CGAL)/lib/$(CGALPLAT) -Wl,-rpath,$(CGAL)/lib/$(CGALPLAT) -lCGAL -lfftw3

.deps:
	mkdir .deps

star.raw: star.png
	convert star.png -resize 64x64 gray:star.raw

%.o: %.cpp .deps
	$(CXX) -c -o $@ -MD -MF .deps/$*.d $(CXXFLAGS) $<


clean:
	rm -f *.o .deps/*.d constellation

-include .deps/*.d
