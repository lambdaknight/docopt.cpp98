//
//  docopt.cpp
//  docopt
//
//  Created by Jared Grubb on 2013-11-03.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#include "docopt.h"
#include "docopt_util.h"
#include "docopt_private.h"

#include "docopt_value.h"

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <cassert>
#include <cstddef>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/pointer_cast.hpp>

using namespace docopt;

DOCOPT_INLINE
std::ostream& docopt::operator<<(std::ostream& os, value const& val)
{
	if (val.isBool()) {
		bool b = val.asBool();
		os << (b ? "true" : "false");
	} else if (val.isLong()) {
		long v = val.asLong();
		os << v;
	} else if (val.isString()) {
		std::string const& str = val.asString();
		os << '"' << str << '"';
	} else if (val.isStringList()) {
		const std::vector<std::string>& list = val.asStringList();
		os << "[";
		bool first = true;
		for(std::vector<std::string>::const_iterator el = list.begin(); el != list.end(); ++el) {
			if (first) {
				first = false;
			} else {
				os << ", ";
			}
			os << '"' << *el << '"';
		}
		os << "]";
	} else {
		os << "null";
	}
	return os;
}

#pragma mark -
#pragma mark Parsing stuff

class Tokens {
public:
	Tokens(std::vector<std::string> tokens, bool isParsingArgv = true)
	: fTokens(tokens),
	  fIndex(0),
	  fIsParsingArgv(isParsingArgv)
	{}

	operator bool() const {
		return fIndex < fTokens.size();
	}

	static Tokens from_pattern(std::string const& source) {
		static const boost::regex re_separators(
			"(?:\\s*)" // any spaces (non-matching subgroup)
			"("
			"[\\[\\]\\(\\)\\|]" // one character of brackets or parens or pipe character
			"|"
			"\\.\\.\\."  // elipsis
			")");

		static const boost::regex re_strings(
			"(?:\\s*)" // any spaces (non-matching subgroup)
			"("
			"\\S*<.*?>"  // strings, but make sure to keep "< >" strings together
			"|"
			"[^<>\\s]+"     // string without <>
			")");

		// We do two stages of regex matching. The '[]()' and '...' are strong delimeters
		// and need to be split out anywhere they occur (even at the end of a token). We
		// first split on those, and then parse the stuff between them to find the string
		// tokens. This is a little harder than the python version, since they have regex.split
		// and we dont have anything like that.

		std::vector<std::string> tokens;
		for(boost::sregex_iterator match(source.begin(), source.end(), re_separators); match != boost::sregex_iterator(); ++match)
		{
			// handle anything before the separator (this is the "stuff" between the delimeters)
			if (match->prefix().matched)
			{
				for(boost::sregex_iterator m(match->prefix().first, match->prefix().second, re_strings); m != boost::sregex_iterator(); ++m)
				{
					tokens.push_back((*m)[1].str());
				}
			}

			// handle the delimter token itself
			if ((*match)[1].matched)
			{
				tokens.push_back((*match)[1].str());
			}
		}

		return Tokens(tokens, false);
	}

	std::string const& current() const {
		if (*this)
			return fTokens[fIndex];

		static std::string const empty;
		return empty;
	}

	std::string the_rest() const {
		if (!*this)
			return "";
		return join(fTokens.begin()+static_cast<std::ptrdiff_t>(fIndex),
				fTokens.end(),
				" ");
	}

	std::string pop() {
		return fTokens.at(fIndex++);
	}

	bool isParsingArgv() const { return fIsParsingArgv; }

	struct OptionError : public std::runtime_error
	{
		OptionError(const std::string& str) : std::runtime_error(str) {}
	};
private:
	std::vector<std::string> fTokens;
	size_t fIndex;
	bool fIsParsingArgv;
};

template<typename T>
bool flatPredicate(Pattern const* p) { return dynamic_cast<T const*>(p) != NULL; }

// Get all instances of 'T' from the pattern
template <typename T>
std::vector<T*> flat_filter(Pattern& pattern) {
	std::vector<Pattern*> flattened = pattern.flat(&flatPredicate<T>);

	// now, we're guaranteed to have T*'s, so just use static_cast
	std::vector<T*> ret;
	for(std::vector<Pattern*>::const_iterator p = flattened.begin(); p != flattened.end(); ++p)
	{
		ret.push_back(static_cast<T*>(*p));
	}
	return ret;
}

static std::vector<std::string> parse_section(std::string const& name, std::string const& source) {
	// ECMAScript regex only has "?=" for a non-matching lookahead. In order to make sure we always have
	// a newline to anchor our matching, we have to avoid matching the final newline of each grouping.
	// Therefore, our regex is adjusted from the docopt Python one to use ?= to match the newlines before
	// the following lines, rather than after.
	boost::regex const re_section_pattern(
		"(?:^|\\n)"  // anchored at a linebreak (or start of string)
		"("
		   "[^\\n]*" + name + "[^\\n]*(?=\\n?)" // a line that contains the name
		   "(?:\\n[ \\t].*?(?=\\n|$))*"         // followed by any number of lines that are indented
		")",
		boost::regex::icase
	);

	std::vector<std::string> ret;
	for(boost::sregex_iterator match(source.begin(), source.end(), re_section_pattern); match != boost::sregex_iterator(); ++match)
	{
		ret.push_back(trim((*match)[1].str()));
	}

	return ret;
}

static bool is_argument_spec(std::string const& token) {
	if (token.empty())
		return false;

	if (token[0]=='<' && token[token.size()-1]=='>')
		return true;

	bool all_of = true;

	for(std::string::const_iterator it = token.begin(); it != token.end(); ++it)
	{
		if(!isupper(*it))
		{
			all_of = false;
			break;
		}
	}

	return all_of;
}

template <typename I>
std::vector<std::string> longOptions(I iter, I end) {
	std::vector<std::string> ret;
	for(I opt = iter; opt != end; ++opt)
	{
		ret.push_back((*opt)->longOption());
	}
	return ret;
}

static PatternList parse_long(Tokens& tokens, std::vector<Option>& options)
{
	// long ::= '--' chars [ ( ' ' | '=' ) chars ] ;
	std::string longOpt, equal;
	value val;

	StringTriplet partitioned = partition(tokens.pop(), "=");
	longOpt = partitioned.first;
	equal = partitioned.second;
	val = partitioned.third;

	assert(starts_with(longOpt, "--"));

	if (equal.empty()) {
		val = value();
	}

	// detect with options match this long option
	std::vector<Option const*> similar;
	for (std::vector<Option>::const_iterator option = options.begin(); option != options.end(); ++option) {
		if (option->longOption()==longOpt)
			similar.push_back(&*option);
	}

	// maybe allow similar options that match by prefix
	if (tokens.isParsingArgv() && similar.empty()) {
		for (std::vector<Option>::const_iterator option = options.begin(); option != options.end(); ++option) {
			if (option->longOption().empty())
				continue;
			if (starts_with(option->longOption(), longOpt))
				similar.push_back(&*option);
		}
	}

	PatternList ret;

	if (similar.size() > 1) { // might be simply specified ambiguously 2+ times?
		std::vector<std::string> prefixes = longOptions(similar.begin(), similar.end());
		std::string error = "'" + longOpt + "' is not a unique prefix: ";
		error.append(join(prefixes.begin(), prefixes.end(), ", "));
		throw Tokens::OptionError(error);
	} else if (similar.empty()) {
		int argcount = equal.empty() ? 0 : 1;
		options.push_back(Option("", longOpt, argcount));

		boost::shared_ptr<Option> o = boost::make_shared<Option>(options.back());
		if (tokens.isParsingArgv()) {
			o->setValue(argcount ? value(val) : value(true));
		}
		ret.push_back(o);
	} else {
		boost::shared_ptr<Option> o = boost::make_shared<Option>(*similar[0]);
		if (o->argCount() == 0) {
			if (val) {
				std::string error = o->longOption() + " must not have an argument";
				throw Tokens::OptionError(error);
			}
		} else {
			if (!val) {
				const std::string& token = tokens.current();
				if (token.empty() || token=="--") {
					std::string error = o->longOption() + " requires an argument";
					throw Tokens::OptionError(error);
				}
				val = tokens.pop();
			}
		}
		if (tokens.isParsingArgv()) {
			o->setValue(val ? val : value(true));
		}
		ret.push_back(o);
	}

	return ret;
}

static PatternList parse_short(Tokens& tokens, std::vector<Option>& options)
{
	// shorts ::= '-' ( chars )* [ [ ' ' ] chars ] ;

	std::string token = tokens.pop();

	assert(starts_with(token, "-"));
	assert(!starts_with(token, "--"));

	std::string::iterator i = token.begin();
	++i; // skip the leading '-'

	PatternList ret;
	while (i != token.end()) {
		std::string shortOpt = (boost::format("-%1%") % *i).str();
		++i;

		std::vector<Option const*> similar;
		for(std::vector<Option>::const_iterator option = options.begin(); option != options.end(); ++option)
		{
			if (option->shortOption()==shortOpt)
				similar.push_back(&*option);
		}

		if (similar.size() > 1) {
			std::string error = shortOpt + " is specified ambiguously "
			+ (boost::format("%1%") % similar.size()).str() + " times";
			throw Tokens::OptionError(error);
		} else if (similar.empty()) {
			options.push_back(Option(shortOpt, "", 0));

			boost::shared_ptr<Option> o = boost::make_shared<Option>(options.back());
			if (tokens.isParsingArgv()) {
				o->setValue(value(true));
			}
			ret.push_back(o);
		} else {
			boost::shared_ptr<Option> o = boost::make_shared<Option>(*similar[0]);
			value val;
			if (o->argCount()) {
				if (i == token.end()) {
					// consume the next token
					const std::string& ttoken = tokens.current();
					if (ttoken.empty() || ttoken=="--") {
						std::string error = shortOpt + " requires an argument";
						throw Tokens::OptionError(error);
					}
					val = tokens.pop();
				} else {
					// consume all the rest
					val = std::string(i, token.end());
					i = token.end();
				}
			}

			if (tokens.isParsingArgv()) {
				o->setValue(val ? val : value(true));
			}
			ret.push_back(o);
		}
	}

	return ret;
}

static PatternList parse_expr(Tokens& tokens, std::vector<Option>& options);

static PatternList parse_atom(Tokens& tokens, std::vector<Option>& options)
{
	// atom ::= '(' expr ')' | '[' expr ']' | 'options'
	//             | long | shorts | argument | command ;

	std::string const& token = tokens.current();

	PatternList ret;

	if (token == "[") {
		tokens.pop();

		PatternList expr = parse_expr(tokens, options);

		std::string trailing = tokens.pop();
		if (trailing != "]") {
			throw DocoptLanguageError("Mismatched '['");
		}

		ret.push_back(boost::make_shared<Optional>(expr));
	} else if (token=="(") {
		tokens.pop();

		PatternList expr = parse_expr(tokens, options);

		std::string trailing = tokens.pop();
		if (trailing != ")") {
			throw DocoptLanguageError("Mismatched '('");
		}

		ret.push_back(boost::make_shared<Required>(expr));
	} else if (token == "options") {
		tokens.pop();
		ret.push_back(boost::make_shared<OptionsShortcut>());
	} else if (starts_with(token, "--") && token != "--") {
		ret = parse_long(tokens, options);
	} else if (starts_with(token, "-") && token != "-" && token != "--") {
		ret = parse_short(tokens, options);
	} else if (is_argument_spec(token)) {
		ret.push_back(boost::make_shared<Argument>(tokens.pop()));
	} else {
		ret.push_back(boost::make_shared<Command>(tokens.pop()));
	}

	return ret;
}

static PatternList parse_seq(Tokens& tokens, std::vector<Option>& options)
{
	// seq ::= ( atom [ '...' ] )* ;"""

	PatternList ret;

	while (tokens) {
		const std::string& token = tokens.current();

		if (token=="]" || token==")" || token=="|")
			break;

		PatternList atom = parse_atom(tokens, options);
		if (tokens.current() == "...") {
			ret.push_back(boost::make_shared<OneOrMore>(atom));
			tokens.pop();
		} else {
			for(PatternList::const_iterator it = atom.begin(); it != atom.end(); ++it)
			{
				ret.push_back(*it);
			}
		}
	}

	return ret;
}

static boost::shared_ptr<Pattern> maybe_collapse_to_required(const PatternList& seq)
{
	if (seq.size()==1) {
		return seq[0];
	}
	return boost::make_shared<Required>(seq);
}

static boost::shared_ptr<Pattern> maybe_collapse_to_either(const PatternList& seq)
{
	if (seq.size()==1) {
		return seq[0];
	}
	return boost::make_shared<Either>(seq);
}

PatternList parse_expr(Tokens& tokens, std::vector<Option>& options)
{
	// expr ::= seq ( '|' seq )* ;

	PatternList seq = parse_seq(tokens, options);

	if (tokens.current() != "|")
		return seq;

	PatternList patternList;
	patternList.push_back(maybe_collapse_to_required(PatternList(seq)));

	while (tokens.current() == "|") {
		tokens.pop();
		seq = parse_seq(tokens, options);
		patternList.push_back(maybe_collapse_to_required(PatternList(seq)));
	}

	PatternList ret;
	ret.push_back(maybe_collapse_to_either(PatternList(patternList)));
	return ret;
}

static Required parse_pattern(std::string const& source, std::vector<Option>& options)
{
	Tokens tokens = Tokens::from_pattern(source);
	PatternList result = parse_expr(tokens, options);

	if (tokens)
		throw DocoptLanguageError("Unexpected ending: '" + tokens.the_rest() + "'");

	assert(result.size() == 1  &&  "top level is always one big");
	return Required(result);
}


static std::string formal_usage(std::string const& section) {
	std::string ret = "(";

	unsigned long i = section.find(':') + 1;  // skip past "usage:"
	std::vector<std::string> parts = split(section, i);
	for(size_t ii = 1; ii < parts.size(); ++ii) {
		if (parts[ii] == parts[0]) {
			ret += " ) | (";
		} else {
			ret.push_back(' ');
			ret += parts[ii];
		}
	}

	ret += " )";
	return ret;
}

static PatternList parse_argv(Tokens tokens, std::vector<Option>& options, bool options_first)
{
	// Parse command-line argument vector.
	//
	// If options_first:
	//    argv ::= [ long | shorts ]* [ argument ]* [ '--' [ argument ]* ] ;
	// else:
	//    argv ::= [ long | shorts | argument ]* [ '--' [ argument ]* ] ;

	PatternList ret;
	while (tokens) {
		const std::string& token = tokens.current();

		if (token=="--") {
			// option list is done; convert all the rest to arguments
			while (tokens) {
				ret.push_back(boost::make_shared<Argument>("", tokens.pop()));
			}
		} else if (starts_with(token, "--")) {
			PatternList parsed = parse_long(tokens, options);
			for(PatternList::const_iterator it = parsed.begin(); it != parsed.end(); ++it)
			{
				ret.push_back(*it);
			}
		} else if (token[0]=='-' && token != "-") {
			PatternList parsed = parse_short(tokens, options);
			for(PatternList::const_iterator it = parsed.begin(); it != parsed.end(); ++it)
			{
				ret.push_back(*it);
			}
		} else if (options_first) {
			// option list is done; convert all the rest to arguments
			while (tokens) {
				ret.push_back(boost::make_shared<Argument>("", tokens.pop()));
			}
		} else {
			ret.push_back(boost::make_shared<Argument>("", tokens.pop()));
		}
	}

	return ret;
}

std::vector<Option> parse_defaults(std::string const& doc) {
	// This pattern is a delimiter by which we split the options.
	// The delimiter is a new line followed by a whitespace(s) followed by one or two hyphens.
	static boost::regex const re_delimiter(
		"(?:^|\\n)[ \\t]*"  // a new line with leading whitespace
		"(?=-{1,2})"        // [split happens here] (positive lookahead) ... and followed by one or two hyphes
	);

	std::vector<Option> defaults;
	std::vector<std::string> parsed = parse_section("options:", doc);
	for(std::vector<std::string>::iterator s = parsed.begin(); s != parsed.end(); ++s)
	{
		s->erase(s->begin(), s->begin() + static_cast<std::ptrdiff_t>(s->find(':')) + 1); // get rid of "options:"

		std::vector<std::string> split = regex_split(*s, re_delimiter);
		for(std::vector<std::string>::const_iterator opt = split.begin(); opt != split.end(); ++opt)
		{
			if (starts_with(*opt, "-")) {
				defaults.push_back(Option::parse(*opt));
			}
		}
	}

	return defaults;
}

static bool isOptionSet(PatternList const& options, std::string const& opt1, std::string const& opt2 = "") {
	for(PatternList::const_iterator opt = options.begin(); opt != options.end(); ++opt)
	{
		const std::string& name = (*opt)->name();
		if ((name==opt1 || (!opt2.empty() && name==opt2)) && (*opt)->hasValue())
		{
			return true;
		}
	}

	return false;
}

static void extras(bool help, bool version, PatternList const& options) {
	if (help && isOptionSet(options, "-h", "--help")) {
		throw DocoptExitHelp();
	}

	if (version && isOptionSet(options, "--version")) {
		throw DocoptExitVersion();
	}
}


// Parse the doc string and generate the Pattern tree
static std::pair<Required, std::vector<Option> > create_pattern_tree(std::string const& doc)
{
	std::vector<std::string> usage_sections = parse_section("usage:", doc);
	if (usage_sections.empty()) {
		throw DocoptLanguageError("'usage:' (case-insensitive) not found.");
	}
	if (usage_sections.size() > 1) {
		throw DocoptLanguageError("More than one 'usage:' (case-insensitive).");
	}

	std::vector<Option> options = parse_defaults(doc);
	Required pattern = parse_pattern(formal_usage(usage_sections[0]), options);

	std::vector<Option const*> pattern_options = flat_filter<Option const>(pattern);

	typedef std::set<Option const*, PatternLess, std::allocator<Option const*> > UniqueOptions;
	UniqueOptions const uniq_pattern_options(pattern_options.begin(), pattern_options.end());

	// Fix up any "[options]" shortcuts with the actual option tree
	std::vector<OptionsShortcut*> filtered = flat_filter<OptionsShortcut>(pattern);
	for(std::vector<OptionsShortcut*>::iterator options_shortcut = filtered.begin(); options_shortcut != filtered.end(); ++options_shortcut)
	{
		std::vector<Option> doc_options = parse_defaults(doc);

		// set(doc_options) - set(pattern_options)
		UniqueOptions uniq_doc_options;
		for(std::vector<Option>::const_iterator opt = doc_options.begin(); opt != doc_options.end(); ++opt)
		{
			if (uniq_pattern_options.count(&*opt))
				continue;
			uniq_doc_options.insert(&*opt);
		}

		// turn into shared_ptr's and set as children
		PatternList children;

		for(UniqueOptions::const_iterator opt = uniq_doc_options.begin(); opt != uniq_doc_options.end(); ++opt)
		{
			children.push_back(boost::make_shared<Option>(**opt));
		}

		(*options_shortcut)->setChildren(children);
	}


	return std::make_pair(pattern, options);
}

DOCOPT_INLINE
std::map<std::string, value>
docopt::docopt_parse(std::string const& doc,
			 std::vector<std::string> const& argv,
			 bool help,
			 bool version,
			 bool options_first)
{
	Required pattern;
	std::vector<Option> options;
	try {
		std::pair<Required, std::vector<Option> > patternTree = create_pattern_tree(doc);
		pattern = patternTree.first;
		options = patternTree.second;
	} catch (Tokens::OptionError const& error) {
		throw DocoptLanguageError(error.what());
	}

	PatternList argv_patterns;
	try {
		argv_patterns = parse_argv(Tokens(argv), options, options_first);
	} catch (Tokens::OptionError const& error) {
		throw DocoptArgumentError(error.what());
	}

	extras(help, version, argv_patterns);

	std::vector<boost::shared_ptr<LeafPattern> > collected;
	bool matched = pattern.fix().match(argv_patterns, collected);
	if (matched && argv_patterns.empty()) {
		std::map<std::string, value> ret;

		// (a.name, a.value) for a in (pattern.flat() + collected)
		std::vector<LeafPattern*> leaves = pattern.leaves();
		for(std::vector<LeafPattern*>::const_iterator p = leaves.begin(); p != leaves.end(); ++p)
		{
			ret[(*p)->name()] = (*p)->getValue();
		}

		for(std::vector<boost::shared_ptr<LeafPattern> >::const_iterator p = collected.begin(); p != collected.end(); ++p)
		{
			ret[(*p)->name()] = (*p)->getValue();
		}

		return ret;
	}

	if (matched) {
		std::string leftover = join(argv.begin(), argv.end(), ", ");
		throw DocoptArgumentError("Unexpected argument: " + leftover);
	}

	throw DocoptArgumentError("Arguments did not match expected patterns"); // BLEH. Bad error.
}

DOCOPT_INLINE
std::map<std::string, value>
docopt::docopt(std::string const& doc,
		   std::vector<std::string> const& argv,
		   bool help,
		   std::string const& version,
		   bool options_first)
{
	try {
		return docopt_parse(doc, argv, help, !version.empty(), options_first);
	} catch (DocoptExitHelp const&) {
		std::cout << doc << std::endl;
		std::exit(0);
	} catch (DocoptExitVersion const&) {
		std::cout << version << std::endl;
		std::exit(0);
	} catch (DocoptLanguageError const& error) {
		std::cerr << "Docopt usage string could not be parsed" << std::endl;
		std::cerr << error.what() << std::endl;
		std::exit(-1);
	} catch (DocoptArgumentError const& error) {
		std::cerr << error.what();
		std::cout << std::endl;
		std::cout << doc << std::endl;
		std::exit(-1);
	} /* Any other exception is unexpected: let std::terminate grab it */
}
