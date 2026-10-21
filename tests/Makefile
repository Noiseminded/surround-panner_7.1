CXX ?= g++
CXXFLAGS = -std=c++11 -O3 -Wall

test: test_panlaw test_binaural
	./test_panlaw
	./test_binaural

test_panlaw: test_panlaw.cpp ../src/PanLaw.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_panlaw.cpp

test_binaural: test_binaural.cpp ../src/Binaural.hpp ../src/HrtfData.hpp ../src/PanLaw.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_binaural.cpp

clean:
	rm -f test_panlaw test_binaural

.PHONY: test clean
