CXX ?= g++

test: test_panlaw
	./test_panlaw

test_panlaw: test_panlaw.cpp ../src/PanLaw.hpp
	$(CXX) -std=c++11 -O2 -Wall -o $@ test_panlaw.cpp

clean:
	rm -f test_panlaw

.PHONY: test clean
