CC = gcc
CXX = g++
CFLAGS := -O2 -g0 -Wall -I.
LDFLAGS := -lm

default:	wavrec-split

wavrec-split:	MultiWaveFile.cpp MultiWaveFile.hpp wavrec-split.cpp
	$(CXX) $(CFLAGS) $^ $(LDFLAGS) -o $@
