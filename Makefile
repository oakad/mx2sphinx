
mx2sphinx: mx2sphinx.o
	g++ -O2 -s -o $@ $< -lboost_program_options -lboost_filesystem

mx2sphinx.o: mx2sphinx.cpp
	g++ -std=c++0x -O2 -c $<
