//
//  docopt.h
//  docopt
//
//  Created by Jared Grubb on 2013-11-03.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#ifndef docopt__docopt_h_
#define docopt__docopt_h_

#include "docopt_value.h"

#include <map>
#include <vector>
#include <string>

#ifdef DOCOPT_HEADER_ONLY
	#define DOCOPT_INLINE inline
	#define DOCOPTAPI
#else 
	#define DOCOPT_INLINE

	// On Windows, export certain symbols so they are available
	// to users of docopt.dll (shared library).
	#ifdef WIN32
		#ifdef DOCOPT_EXPORTS
			#define DOCOPTAPI __declspec(dllexport)
		#else
			#define DOCOPTAPI __declspec(dllimport)
		#endif
	#else
		#define DOCOPTAPI
	#endif
#endif

namespace docopt {

	// Usage string could not be parsed (ie, the developer did something wrong)
	struct DocoptLanguageError : std::runtime_error
	{
		DocoptLanguageError(const std::string& str) : std::runtime_error(str) {}
	};

	// Arguments passed by user were incorrect (ie, developer was good, user is wrong)
	struct DocoptArgumentError : std::runtime_error
	{
		DocoptArgumentError(const std::string& str) : std::runtime_error(str) {}
	};

	// Arguments contained '--help' and parsing was aborted early
	struct DocoptExitHelp : std::runtime_error { DocoptExitHelp() : std::runtime_error("Docopt --help argument encountered"){} };

	// Arguments contained '--version' and parsing was aborted early
	struct DocoptExitVersion : std::runtime_error { DocoptExitVersion() : std::runtime_error("Docopt --version argument encountered") {} };
	
	/// Parse user options from the given option string.
	///
	/// @param doc   The usage string
	/// @param argv  The user-supplied arguments
	/// @param help  Whether to end early if '-h' or '--help' is in the argv
	/// @param version Whether to end early if '--version' is in the argv
	/// @param options_first  Whether options must precede all args (true), or if args and options
	///                can be arbitrarily mixed.
	///
	/// @throws DocoptLanguageError if the doc usage string had errors itself
	/// @throws DocoptExitHelp if 'help' is true and the user has passed the '--help' argument
	/// @throws DocoptExitVersion if 'version' is true and the user has passed the '--version' argument
	/// @throws DocoptArgumentError if the user's argv did not match the usage patterns
	std::map<std::string, value> DOCOPTAPI docopt_parse(std::string const& doc,
						std::vector<std::string> const& argv,
						bool help = true,
						bool version = true,
						bool options_first = false);
	
	/// Parse user options from the given string, and exit appropriately
	///
	/// Calls 'docopt_parse' and will terminate the program if any of the exceptions above occur:
	///  * DocoptLanguageError - print error and terminate (with exit code -1)
	///  * DocoptExitHelp - print usage string and terminate (with exit code 0)
	///  * DocoptExitVersion - print version and terminate (with exit code 0)
	///  * DocoptArgumentError - print error and usage string and terminate (with exit code -1)
	std::map<std::string, value> DOCOPTAPI docopt(std::string const& doc,
						std::vector<std::string> const& argv,
						bool help = true,
						std::string const& version = "",
						bool options_first = false);
}

#ifdef DOCOPT_HEADER_ONLY
	#include "docopt.cpp"
#endif

#endif /* defined(docopt__docopt_h_) */
