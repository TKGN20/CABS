#scl enable devtoolset-9 bash

CC=g++ -std=c++17 -Ofast -lrt -DNDEBUG  -DHAVE_CXX0X -march=native -fpic -w -I. -I./src -ftree-vectorize -ftree-vectorizer-verbose=0
CCOMP=g++ -std=c++17 -mavx512f -Ofast -lrt -DNDEBUG  -DHAVE_CXX0X -openmp -march=native -fpic -w -I. -I./src -fopenmp -ftree-vectorize -ftree-vectorizer-verbose=0
SRCS=$(wildcard ./src/*.cpp)

TARGET=sos
RESDIR=./results

.PHONY: $(TARGET) clean cleancodes

sos: $(SRCS)
	rm -rf $(TARGET)
	mkdir -p $(RESDIR)
	$(CCOMP) $(SRCS) -o $(TARGET)

clean:
	rm -rf $(TARGET)
	rm -rf $(wildcard *.txt)
	# keep datasets and results on clean

cleancodes:
	rm -rf $(wildcard *.cpp) $(wildcard *.h) $(wildcard ./src/*.cpp) $(wildcard ./src/*.h)
