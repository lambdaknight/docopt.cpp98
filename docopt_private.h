//
//  docopt_private.h
//  docopt
//
//  Created by Jared Grubb on 2013-11-04.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#ifndef docopt_docopt_private_h
#define docopt_docopt_private_h

#include <vector>
#include <memory>
#include <unordered_set>
#include <assert.h>


#ifdef BOOST_NO_CXX11_FINAL
#    define OVERRIDE
#    define FINAL
#else
#    define OVERRIDE override
#    define FINAL final
#endif

// Workaround GCC 4.8 not having std::regex
#if DOCTOPT_USE_BOOST_REGEX
#include <boost/regex.hpp>
namespace std {
	using boost::regex;
	using boost::sregex_iterator;
	using boost::smatch;
	using boost::regex_search;
	namespace regex_constants {
		using boost::regex_constants::match_not_null;
	}
}
#else
#include <regex>
#endif

#include <boost/make_shared.hpp>

#include "docopt_value.h"

namespace docopt {

	class Pattern;
	class LeafPattern;

	using PatternList = std::vector<boost::shared_ptr<Pattern> >;

	// Utility to use Pattern types in std hash-containers
	struct PatternHasher {
		template <typename P>
		size_t operator()(boost::shared_ptr<P> const& pattern) const {
			return pattern->hash();
		}
		template <typename P>
		size_t operator()(P const* pattern) const {
			return pattern->hash();
		}
		template <typename P>
		size_t operator()(P const& pattern) const {
			return pattern.hash();
		}
	};

	// Utility to use 'hash' as the equality operator as well in std containers
	struct PatternPointerEquality {
		template <typename P1, typename P2>
		bool operator()(boost::shared_ptr<P1> const& p1, boost::shared_ptr<P2> const& p2) const {
			return p1->hash()==p2->hash();
		}
		template <typename P1, typename P2>
		bool operator()(P1 const* p1, P2 const* p2) const {
			return p1->hash()==p2->hash();
		}
	};

	// A hash-set that uniques by hash value
	using UniquePatternSet = std::unordered_set<boost::shared_ptr<Pattern>, PatternHasher, PatternPointerEquality>;


	class Pattern {
	public:
		// flatten out children, stopping descent when the given filter returns 'true'
		virtual std::vector<Pattern*> flat(bool (*filter)(Pattern const*)) = 0;

		// flatten out all children into a list of LeafPattern objects
		virtual void collect_leaves(std::vector<LeafPattern*>&) = 0;

		// flatten out all children into a list of LeafPattern objects
		std::vector<LeafPattern*> leaves();

		// Attempt to find something in 'left' that matches this pattern's spec, and if so, move it to 'collected'
		virtual bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const = 0;

		virtual std::string const& name() const = 0;

		virtual bool hasValue() const { return false; }

		virtual size_t hash() const = 0;
	};

	class LeafPattern
	: public Pattern {
	public:
		LeafPattern(std::string name, value v = value())
		: fName(std::move(name)),
		  fValue(std::move(v))
		{}

		virtual std::vector<Pattern*> flat(bool (*filter)(Pattern const*)) OVERRIDE {
			std::vector<Pattern*> ret;
			if (filter(this)) {
				ret.push_back(this);
			}
			return ret;
		}

		virtual void collect_leaves(std::vector<LeafPattern*>& lst) OVERRIDE FINAL {
			lst.push_back(this);
		}

		virtual bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const OVERRIDE;

		virtual bool hasValue() const OVERRIDE { return static_cast<bool>(fValue); }

		value const& getValue() const { return fValue; }
		void setValue(value&& v) { fValue = std::move(v); }

		virtual std::string const& name() const OVERRIDE { return fName; }

		virtual size_t hash() const OVERRIDE {
			size_t seed = typeid(*this).hash_code();
			hash_combine(seed, fName);
			hash_combine(seed, fValue);
			return seed;
		}

	protected:
		virtual std::pair<size_t, boost::shared_ptr<LeafPattern> > single_match(PatternList const&) const = 0;

	private:
		std::string fName;
		value fValue;
	};

	class BranchPattern
	: public Pattern {
	public:
		BranchPattern(PatternList children = PatternList())
		: fChildren(std::move(children))
		{}

		Pattern& fix() {
			UniquePatternSet patterns;
			fix_identities(patterns);
			fix_repeating_arguments();
			return *this;
		}

		virtual std::string const& name() const OVERRIDE {
			throw std::runtime_error("Logic error: name() shouldnt be called on a BranchPattern");
		}

		virtual value const& getValue() const {
			throw std::runtime_error("Logic error: name() shouldnt be called on a BranchPattern");
		}

		virtual std::vector<Pattern*> flat(bool (*filter)(Pattern const*)) OVERRIDE {
			std::vector<Pattern*> ret;
			if (filter(this)) {
				ret.push_back(this);
				return ret;
			}


			for(PatternList::iterator child = fChildren.begin(); child != fChildren.end(); ++child)
			{
				std::vector<Pattern*> sublist = (*child)->flat(filter);
				ret.insert(ret.end(), sublist.begin(), sublist.end());
			}

			return ret;
		}

		virtual void collect_leaves(std::vector<LeafPattern*>& lst) OVERRIDE FINAL {
			for(PatternList::iterator child = fChildren.begin(); child != fChildren.end(); ++child)
			{
				(*child)->collect_leaves(lst);
			}
		}

		void setChildren(PatternList children) {
			fChildren = std::move(children);
		}

		PatternList const& children() const { return fChildren; }

		virtual void fix_identities(UniquePatternSet& patterns) {
			for(PatternList::iterator child = fChildren.begin(); child != fChildren.end(); ++child)
			{
				// this will fix up all its children, if needed
				if (BranchPattern* bp = dynamic_cast<BranchPattern*>(child->get())) {
					bp->fix_identities(patterns);
				}

				// then we try to add it to the list
				std::pair<std::unordered_set<boost::shared_ptr<Pattern>, PatternHasher, PatternPointerEquality>::iterator, bool> inserted = patterns.insert(*child);
				if (!inserted.second) {
					// already there? then reuse the existing shared_ptr for that thing
					*child = *inserted.first;
				}
			}
		}

		virtual size_t hash() const OVERRIDE {
			size_t seed = typeid(*this).hash_code();
			hash_combine(seed, fChildren.size());
			for(PatternList::const_iterator child = fChildren.begin(); child != fChildren.end(); ++child)
			{
				hash_combine(seed, (*child)->hash());
			}
			return seed;
		}
	private:
		void fix_repeating_arguments();

	protected:
		PatternList fChildren;
	};

	class Argument
	: public LeafPattern {
	public:
		Argument(std::string name, value v = value()) : LeafPattern(name, v) {}

	protected:
		virtual std::pair<size_t, boost::shared_ptr<LeafPattern> > single_match(PatternList const& left) const OVERRIDE;
	};

	class Command : public Argument {
	public:
		Command(std::string name, value v = value(false))
		: Argument(std::move(name), std::move(v))
		{}

	protected:
		virtual std::pair<size_t, boost::shared_ptr<LeafPattern> > single_match(PatternList const& left) const OVERRIDE;
	};

	class Option FINAL
	: public LeafPattern
	{
	public:
		static Option parse(std::string const& option_description);

		Option(std::string shortOption,
			   std::string longOption,
			   int argcount = 0,
			   value v = value(false))
		: LeafPattern(longOption.empty() ? shortOption : longOption,
				  std::move(v)),
		  fShortOption(std::move(shortOption)),
		  fLongOption(std::move(longOption)),
		  fArgcount(argcount)
		{
			// From Python:
			//   self.value = None if value is False and argcount else value
			if (argcount && v.isBool() && !v.asBool()) {
				setValue(value());
			}
		}

		Option(Option const&) = default;
		Option(Option&&) = default;
		Option& operator=(Option const&) = default;
		Option& operator=(Option&&) = default;

		using LeafPattern::setValue;

		std::string const& longOption() const { return fLongOption; }
		std::string const& shortOption() const { return fShortOption; }
		int argCount() const { return fArgcount; }

		virtual size_t hash() const OVERRIDE {
			size_t seed = LeafPattern::hash();
			hash_combine(seed, fShortOption);
			hash_combine(seed, fLongOption);
			hash_combine(seed, fArgcount);
			return seed;
		}

	protected:
		virtual std::pair<size_t, boost::shared_ptr<LeafPattern> > single_match(PatternList const& left) const OVERRIDE;

	private:
		std::string fShortOption;
		std::string fLongOption;
		int fArgcount;
	};

	class Required : public BranchPattern {
	public:
		Required(PatternList children = PatternList()) : BranchPattern(children) {}

		bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const OVERRIDE;
	};

	class Optional : public BranchPattern {
	public:
		Optional(PatternList children = PatternList()) : BranchPattern(children) {}

		bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const OVERRIDE {
			for(PatternList::const_iterator pattern = fChildren.begin(); pattern != fChildren.end(); ++pattern)
			{
				(*pattern)->match(left, collected);
			}
			return true;
		}
	};

	class OptionsShortcut : public Optional {
	public:
		OptionsShortcut(PatternList children = PatternList()) : Optional(children) {}
	};

	class OneOrMore : public BranchPattern {
	public:
		OneOrMore(PatternList children = PatternList()) : BranchPattern(children) {}

		bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const OVERRIDE;
	};

	class Either : public BranchPattern {
	public:
		Either(PatternList children = PatternList()) : BranchPattern(children) {}

		bool match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const OVERRIDE;
	};

#pragma mark -
#pragma mark inline implementations

	inline std::vector<LeafPattern*> Pattern::leaves()
	{
		std::vector<LeafPattern*> ret;
		collect_leaves(ret);
		return ret;
	}

	static inline std::vector<PatternList> transform(PatternList pattern)
	{
		std::vector<PatternList> result;

		std::vector<PatternList> groups;
		groups.push_back(std::move(pattern));

		while(!groups.empty()) {
			// pop off the first element
			std::vector<boost::shared_ptr<Pattern> > children = std::move(groups[0]);
			groups.erase(groups.begin());

			// find the first branch node in the list
			std::vector<boost::shared_ptr<Pattern> >::iterator child_iter = children.begin();
			for (; child_iter != children.end(); ++child_iter)
			{
				if (dynamic_cast<BranchPattern const*>(child_iter->get()))
					break;
			}

			// no branch nodes left : expansion is complete for this grouping
			if (child_iter == children.end()) {
				result.push_back(std::move(children));
				continue;
			}

			// pop the child from the list
			boost::shared_ptr<Pattern> child = std::move(*child_iter);
			children.erase(child_iter);

			// expand the branch in the appropriate way
			if (Either* either = dynamic_cast<Either*>(child.get())) {
				// "[e] + children" for each child 'e' in Either
				for (PatternList::const_iterator eitherChild = either->children().begin(); eitherChild != either->children().end(); ++eitherChild)
				{
					PatternList group;
					group.push_back(*eitherChild);
					group.insert(group.end(), children.begin(), children.end());

					groups.push_back(std::move(group));
				}
			} else if (OneOrMore* oneOrMore = dynamic_cast<OneOrMore*>(child.get())) {
				// child.children * 2 + children
				const PatternList& subchildren = oneOrMore->children();
				PatternList group = subchildren;
				group.insert(group.end(), subchildren.begin(), subchildren.end());
				group.insert(group.end(), children.begin(), children.end());

				groups.push_back(std::move(group));
			} else { // Required, Optional, OptionsShortcut
				BranchPattern* branch = dynamic_cast<BranchPattern*>(child.get());

				// child.children + children
				PatternList group = branch->children();
				group.insert(group.end(), children.begin(), children.end());

				groups.push_back(std::move(group));
			}
		}

		return result;
	}

	inline void BranchPattern::fix_repeating_arguments()
	{
		std::vector<PatternList> either = transform(children());
		for(std::vector<PatternList>::const_iterator group = either.begin(); group != either.end(); ++group)
		{
			// use multiset to help identify duplicate entries
			std::unordered_multiset<boost::shared_ptr<Pattern>, PatternHasher> group_set(group->begin(), group->end());
			for(std::unordered_multiset<boost::shared_ptr<Pattern>, PatternHasher>::const_iterator e = group_set.begin(); e != group_set.end(); ++e) {
				if (group_set.count(*e) == 1)
					continue;

				LeafPattern* leaf = dynamic_cast<LeafPattern*>(e->get());
				if (!leaf) continue;

				bool ensureList = false;
				bool ensureInt = false;

				if (dynamic_cast<Command*>(leaf)) {
					ensureInt = true;
				} else if (dynamic_cast<Argument*>(leaf)) {
					ensureList = true;
				} else if (Option* o = dynamic_cast<Option*>(leaf)) {
					if (o->argCount()) {
						ensureList = true;
					} else {
						ensureInt = true;
					}
				}

				if (ensureList) {
					std::vector<std::string> newValue;
					if (leaf->getValue().isString()) {
						newValue = split(leaf->getValue().asString());
					}
					if (!leaf->getValue().isStringList()) {
						leaf->setValue(value(newValue));
					}
				} else if (ensureInt) {
					leaf->setValue(value(0));
				}
			}
		}
	}

	inline bool LeafPattern::match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const
	{
		std::pair<size_t, boost::shared_ptr<LeafPattern> > match = single_match(left);
		if (!match.second) {
			return false;
		}

		left.erase(left.begin()+static_cast<std::ptrdiff_t>(match.first));

		std::vector<boost::shared_ptr<LeafPattern> >::iterator same_name = collected.begin();
		for(; same_name != collected.end(); ++same_name)
		{
			if((*same_name)->name() == name())
				break;
		}
		if (getValue().isLong()) {
			long val = 1;
			if (same_name == collected.end()) {
				collected.push_back(match.second);
				match.second->setValue(value(val));
			} else if ((**same_name).getValue().isLong()) {
				val += (**same_name).getValue().asLong();
				(**same_name).setValue(value(val));
			} else {
				(**same_name).setValue(value(val));
			}
		} else if (getValue().isStringList()) {
			std::vector<std::string> val;
			if (match.second->getValue().isString()) {
				val.push_back(match.second->getValue().asString());
			} else if (match.second->getValue().isStringList()) {
				val = match.second->getValue().asStringList();
			} else {
				/// cant be!?
			}

			if (same_name == collected.end()) {
				collected.push_back(match.second);
				match.second->setValue(value(val));
			} else if ((**same_name).getValue().isStringList()) {
				std::vector<std::string> const& list = (**same_name).getValue().asStringList();
				val.insert(val.begin(), list.begin(), list.end());
				(**same_name).setValue(value(val));
			} else {
				(**same_name).setValue(value(val));
			}
		} else {
			collected.push_back(match.second);
		}
		return true;
	}

	inline std::pair<size_t, boost::shared_ptr<LeafPattern> > Argument::single_match(PatternList const& left) const
	{
		std::pair<size_t, boost::shared_ptr<LeafPattern> > ret;

		for(size_t i = 0, size = left.size(); i < size; ++i)
		{
			const Argument* arg = dynamic_cast<Argument const*>(left[i].get());
			if (arg) {
				ret.first = i;
				ret.second = boost::make_shared<Argument>(name(), arg->getValue());
				break;
			}
		}

		return ret;
	}

	inline std::pair<size_t, boost::shared_ptr<LeafPattern> > Command::single_match(PatternList const& left) const
	{
		std::pair<size_t, boost::shared_ptr<LeafPattern> > ret;

		for(size_t i = 0, size = left.size(); i < size; ++i)
		{
			const Argument* arg = dynamic_cast<Argument const*>(left[i].get());
			if (arg) {
				if (name() == arg->getValue()) {
					ret.first = i;
					ret.second = boost::make_shared<Command>(name(), value(true));
				}
				break;
			}
		}

		return ret;
	}

	inline Option Option::parse(std::string const& option_description)
	{
		std::string shortOption, longOption;
		int argcount = 0;
		value val(false);

		unsigned long double_space = option_description.find("  ");
		std::string::const_iterator options_end = option_description.end();
		if (double_space != std::string::npos) {
			options_end = option_description.begin() + static_cast<std::ptrdiff_t>(double_space);
		}

		static const std::regex pattern("(-{1,2})?(.*?)([,= ]|$)");
		for(std::sregex_iterator i(option_description.begin(), options_end, pattern, std::regex_constants::match_not_null),
			e;
			i != e;
			++i)
		{
			std::smatch const& match = *i;
			if (match[1].matched) { // [1] is optional.
				if (match[1].length()==1) {
						shortOption = "-" + match[2].str();
				} else {
						longOption =  "--" + match[2].str();
				}
			} else if (match[2].length() > 0) { // [2] always matches.
				std::string m = match[2];
				argcount = 1;
			} else {
				// delimeter
			}

			if (match[3].length() == 0) { // [3] always matches.
				// Hit end of string. For some reason 'match_not_null' will let us match empty
				// at the end, and then we'll spin in an infinite loop. So, if we hit an empty
				// match, we know we must be at the end.
				break;
			}
		}

		if (argcount) {
			std::smatch match;
			if (std::regex_search(options_end, option_description.end(),
						  match,
						  std::regex("\\[default: (.*)\\]", std::regex::icase)))
			{
				val = match[1].str();
			}
		}

		return Option(std::move(shortOption), std::move(longOption), argcount, std::move(val));
	}

	inline std::pair<size_t, boost::shared_ptr<LeafPattern> > Option::single_match(PatternList const& left) const
	{
		std::pair<size_t, boost::shared_ptr<LeafPattern> > ret;

		PatternList::const_iterator thematch = left.begin();
		for (; thematch != left.end(); ++thematch)
		{
			boost::shared_ptr<LeafPattern> leaf = boost::dynamic_pointer_cast<LeafPattern>(*thematch);
			if (leaf && this->name() == leaf->name())
				break;
		}

		if (thematch == left.end())
		{
			return ret;
		}
		ret.first = std::distance(left.begin(), thematch);
		ret.second = boost::dynamic_pointer_cast<LeafPattern>(*thematch);
		return ret;
	}

	inline bool Required::match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const {
		PatternList l = left;
		std::vector<boost::shared_ptr<LeafPattern> > c = collected;

		for(PatternList::const_iterator pattern = fChildren.begin(); pattern != fChildren.end(); ++pattern)
		{
			bool ret = (*pattern)->match(l, c);
			if (!ret) {
				// leave (left, collected) untouched
				return false;
			}
		}

		left = std::move(l);
		collected = std::move(c);
		return true;
	}

	inline bool OneOrMore::match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const
	{
		assert(fChildren.size() == 1);

		PatternList l = left;
		std::vector<boost::shared_ptr<LeafPattern> > c = collected;

		bool matched = true;
		size_t times = 0;

		decltype(l) l_;
		bool firstLoop = true;

		while (matched) {
			// could it be that something didn't match but changed l or c?
			matched = fChildren[0]->match(l, c);

			if (matched)
				++times;

			if (firstLoop) {
				firstLoop = false;
			} else if (l == l_) {
				break;
			}

			l_ = l;
		}

		if (times == 0) {
			return false;
		}

		left = std::move(l);
		collected = std::move(c);
		return true;
	}

	inline bool Either::match(PatternList& left, std::vector<boost::shared_ptr<LeafPattern> >& collected) const
	{
		using Outcome = std::pair<PatternList, std::vector<boost::shared_ptr<LeafPattern> > >;

		std::vector<Outcome> outcomes;

		for (PatternList::const_iterator pattern = fChildren.begin(); pattern != fChildren.end(); ++pattern)
		{
			// need a copy so we apply the same one for every iteration
			PatternList l = left;
			std::vector<boost::shared_ptr<LeafPattern> > c = collected;
			bool matched = (*pattern)->match(l, c);
			if (matched) {
				outcomes.push_back(Outcome(std::move(l), std::move(c)));
			}
		}

		unsigned long currentMinimum = ULONG_MAX;
		Outcome minOutcome;

		for (std::vector<Outcome>::const_iterator it = outcomes.begin(); it != outcomes.end(); ++it)
		{
			if (it->first.size() < currentMinimum)
			{
				currentMinimum = it->first.size();
				minOutcome = *it;
			}
		}

		if (currentMinimum == ULONG_MAX)
		{
			return false;
		}

		left = minOutcome.first;
		collected = minOutcome.second;

		return true;
	}

}

#endif
