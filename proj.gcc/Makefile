# 
# Makefile for lsignal
#

CXX?=g++
#CXXFLAGS?=-std=c++17 -O3 -Wall
CXXFLAGS?=-std=c++17 -O0 -Wall
LDFLAGS?=
EXECUTABLE=lsignal
SOURCES=../tests/tests.cpp \
	../tests/test_basic.cpp \
	../tests/test_multithread.cpp \
	../lsignal.cpp

OBJECTS=$(SOURCES:.cpp=.o)

.PHONY: all
all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@

%.o : %.cpp lsignal.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o ../*.o ../tests/*.o $(EXECUTABLE)

