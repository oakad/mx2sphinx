
mx2sphinx: mx2sphinx.cpp
	g++ -std=c++0x -O2 -s -o $@ $< -lboost_program_options -lboost_filesystem
