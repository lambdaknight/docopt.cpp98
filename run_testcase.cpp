//
//  docopt_tests.cpp
//  docopt
//
//  Created by Jared Grubb on 2013-11-03.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#include "docopt.h"

#include <iostream>

int main(int argc, const char** argv)
{
	if (argc < 2) {
		std::cerr << "Usage: docopt_tests USAGE [arg]..." << std::endl;
		exit(-5);
	}

	std::string usage = argv[1];
	std::vector<std::string> args(argv+2, argv+argc);

	std::map<std::string, docopt::value> result = docopt::docopt(usage, args);

	// print it out in JSON form
	std::cout << "{ ";
	bool first = true;

	for(std::map<std::string, docopt::value>::const_iterator arg = result.begin(); arg != result.end(); ++arg)
	{
		if(first) {
			first = false;
		} else {
			std::cout << "," << std::endl;
		}

		std::cout << '"' << arg->first << '"' << ": " << arg->second;

	}

	std::cout << " }" << std::endl;

	return 0;
}