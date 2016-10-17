#include "docopt.h"

#include <iostream>

static const char USAGE[] =
"Naval Fate.\n"
"\n"
"    Usage:\n"
"      naval_fate ship new <name>...\n"
"      naval_fate ship <name> move <x> <y> [--speed=<kn>]\n"
"      naval_fate ship shoot <x> <y>\n"
"      naval_fate mine (set|remove) <x> <y> [--moored | --drifting]\n"
"      naval_fate (-h | --help)\n"
"      naval_fate --version\n"
"\n"
"    Options:\n"
"      -h --help     Show this screen.\n"
"      --version     Show version.\n"
"      --speed=<kn>  Speed in knots [default: 10].\n"
"      --moored      Moored (anchored) mine.\n"
"      --drifting    Drifting mine.\n";

int main(int argc, const char** argv)
{
	std::map<std::string, docopt::value> args = docopt::docopt(USAGE, 
												  std::vector<std::string>(argv + 1, argv + argc),
												  true,               // show help if requested
												  "Naval Fate 2.0");  // version string

	for(std::map<std::string, docopt::value>::const_iterator arg = args.begin(); arg != args.end(); ++arg)
	{
		std::cout << arg->first << ": " << arg->second << std::endl;
	}

	return 0;
}
