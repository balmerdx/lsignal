# 
# Makefile for lsignal
#

CXX?=g++
#CXXFLAGS?=-std=c++11 -O3 -Wall
CXXFLAGS?=-std=c++11 -O0 -Wall
LDFLAGS?=
EXECUTABLE=lsignal
SOURCES=tests.cpp lsignal.cpp
OBJECTS=$(SOURCES:.cpp=.o)

.PHONY: all
all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@

%.o : %.cpp lsignal.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(EXECUTABLE)

