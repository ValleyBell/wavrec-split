CC = gcc
CXX = g++
CFLAGS := -O2 -g0 -Wall -I.
LDFLAGS := -lm

default:	wavrec-split

wavrec-split:	$(wildcard *.cpp *.hpp *.h)
	$(CXX) $(CFLAGS) $^ $(LDFLAGS) -o $@
