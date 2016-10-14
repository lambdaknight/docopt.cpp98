//
//  docopt_util.h
//  docopt
//
//  Created by Jared Grubb on 2013-11-04.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#ifndef docopt_docopt_util_h
#define docopt_docopt_util_h

#if DOCTOPT_USE_BOOST_REGEX
#include <boost/regex.hpp>
namespace std {
	using boost::regex;
	using boost::sregex_token_iterator;
}
#else
#include <regex>
#endif

#pragma mark -
#pragma mark General utility

namespace {
	struct StringTriplet
	{
		std::string first;
		std::string second;
		std::string third;
	};

	bool starts_with(std::string const& str, std::string const& prefix)
	{
		if (str.length() < prefix.length())
			return false;
		return std::equal(prefix.begin(), prefix.end(),
				  str.begin());
	}

	std::string trim(std::string&& str,
			 const std::string& whitespace = " \t\n")
	{
		const unsigned long strEnd = str.find_last_not_of(whitespace);
		if (strEnd==std::string::npos)
			return ""; // no content
		str.erase(strEnd+1);

		const unsigned long strBegin = str.find_first_not_of(whitespace);
		str.erase(0, strBegin);

		return std::move(str);
	}

	std::vector<std::string> split(std::string const& str, size_t pos = 0)
	{
		const char* const anySpace = " \t\r\n\v\f";

		std::vector<std::string> ret;
		while (pos != std::string::npos) {
			unsigned long start = str.find_first_not_of(anySpace, pos);
			if (start == std::string::npos) break;

			unsigned long end = str.find_first_of(anySpace, start);
			unsigned long size = end == std::string::npos ? end : end - start;
			ret.push_back(str.substr(start, size));

			pos = end;
		}

		return ret;
	}

	StringTriplet partition(std::string str, std::string const& point)
	{
		StringTriplet ret;

		unsigned long i = str.find(point);

		if (i == std::string::npos) {
			// no match: string goes in 0th spot only
		} else {
			ret.third = str.substr(i + point.size());
			ret.second = point;
			str.resize(i);
		}
		ret.first = std::move(str);

		return ret;
	}

	template <typename I>
	std::string join(I iter, I end, std::string const& delim) {
		if (iter==end)
			return "";

		std::string ret = *iter;
		for(++iter; iter!=end; ++iter) {
			ret.append(delim);
			ret.append(*iter);
		}
		return ret;
	}

	std::vector<std::string> regex_split(std::string const& text, std::regex const& re)
	{
		std::vector<std::string> ret;
		for (std::sregex_token_iterator it(text.begin(), text.end(), re, -1);
			 it != std::sregex_token_iterator();
			 ++it) {
			ret.push_back(*it);
		}
		return ret;
	}
}

namespace docopt {
	template <class T>
	inline void hash_combine(std::size_t& seed, T const& v)
	{
		// stolen from boost::hash_combine
		std::hash<T> hasher;
		seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
	}
}

#endif
