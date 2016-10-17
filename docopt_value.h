//
//  value.h
//  docopt
//
//  Created by Jared Grubb on 2013-10-14.
//  Copyright (c) 2013 Jared Grubb. All rights reserved.
//

#ifndef docopt__value_h_
#define docopt__value_h_

#include <string>
#include <vector>
#include <functional> // std::hash
#include <iosfwd>
#include <typeinfo>
#include <stdexcept>

#include <boost/config.hpp>
#include <boost/functional/hash.hpp>
#include <cstdlib>

namespace docopt {

	/// A generic type to hold the various types that can be produced by docopt.
	///
	/// This type can be one of: {bool, long, string, vector<string>}, or empty.
	struct value {
		/// An empty value
		value() : kind(Empty) {}

		value(std::string);
		value(std::vector<std::string>);
		
		explicit value(bool);
		explicit value(long);
		explicit value(int v);
		
		// Test if this object has any contents at all
		operator bool() const { return kind != Empty; }
		
		// Test the type contained by this value object
		bool isBool()       const { return kind==Bool; }
		bool isString()     const { return kind==String; }
		bool isLong()       const { return kind==Long; }
		bool isStringList() const { return kind==StringList; }

		// Throws std::invalid_argument if the type does not match
		bool asBool() const;
		long asLong() const;
		std::string const& asString() const;
		std::vector<std::string> const& asStringList() const;

		size_t hash() const;
		
		// equality is based on hash-equality
		friend bool operator==(value const&, value const&);
		friend bool operator!=(value const&, value const&);

	private:
		enum Kind {
			Empty,
			Bool,
			Long,
			String,
			StringList
		};
		
		struct Variant {
			bool boolValue;
			long longValue;
			std::string strValue;
			std::vector<std::string> strList;
		};
		
		static const char* kindAsString(Kind kind) {
			switch (kind) {
				case Empty: return "empty";
				case Bool: return "bool";
				case Long: return "long";
				case String: return "string";
				case StringList: return "string-list";
			}
			return "unknown";
		}

		void throwIfNotKind(Kind expected) const {
			if (kind == expected)
				return;

			std::string error = "Illegal cast to ";
			error += kindAsString(expected);
			error += "; type is actually ";
			error += kindAsString(kind);
			throw std::runtime_error(error);
		}

	private:
		Kind kind;
		Variant variant;
	};

	/// Write out the contents to the ostream
	std::ostream& operator<<(std::ostream&, value const&);
}

namespace boost {
	template <>
	struct hash<docopt::value> {
		size_t operator()(docopt::value const& val) const {
			return val.hash();
		}
	};

	template <>
	struct hash<std::type_info> {
		size_t operator()(std::type_info const& val) const {
			return boost::hash<std::string>()(std::string(val.name()));
		}
	};
}

namespace docopt {
	inline
	value::value(bool v)
	: kind(Bool)
	{
		variant.boolValue = v;
	}

	inline
	value::value(long v)
	: kind(Long)
	{
		variant.longValue = v;
	}

	inline
	value::value(int v)
	: kind(Long)
	{
		variant.longValue = static_cast<long>(v);
	}

	inline
	value::value(std::string v)
	: kind(String)
	{
		variant.strValue = v;
	}

	inline
	value::value(std::vector<std::string> v)
	: kind(StringList)
	{
		variant.strList = v;
	}

	inline
	size_t value::hash() const
	{
		switch (kind) {
			case String:
				return boost::hash<std::string>()(variant.strValue);

			case StringList: {
				size_t seed = boost::hash<size_t>()(variant.strList.size());
				for(std::vector<std::string>::const_iterator it = variant.strList.begin(); it != variant.strList.end(); ++it)
				{
					boost::hash_combine(seed, *it);
				}
				return seed;
			}

			case Bool:
				return boost::hash<bool>()(variant.boolValue);

			case Long:
				return boost::hash<long>()(variant.longValue);

			case Empty:
			default:
				return boost::hash<void*>()(NULL);
		}
	}

	inline
	bool value::asBool() const
	{
		throwIfNotKind(Bool);
		return variant.boolValue;
	}

	inline
	long value::asLong() const
	{
		// Attempt to convert a string to a long
		if (kind == String) {
			const std::string& str = variant.strValue;
			std::size_t pos;
			char* pEnd;
			const long ret = std::strtol(str.c_str(), &pEnd, 10);
			if (pos != str.length()) {
				// The string ended in non-digits.
				throw std::runtime_error( str + " contains non-numeric characters.");
			}
			return ret;
		}
		throwIfNotKind(Long);
		return variant.longValue;
	}

	inline
	std::string const& value::asString() const
	{
		throwIfNotKind(String);
		return variant.strValue;
	}

	inline
	std::vector<std::string> const& value::asStringList() const
	{
		throwIfNotKind(StringList);
		return variant.strList;
	}

	inline
	bool operator==(value const& v1, value const& v2)
	{
		if (v1.kind != v2.kind)
			return false;
		
		switch (v1.kind) {
			case value::String:
				return v1.variant.strValue==v2.variant.strValue;

			case value::StringList:
				return v1.variant.strList==v2.variant.strList;

			case value::Bool:
				return v1.variant.boolValue==v2.variant.boolValue;

			case value::Long:
				return v1.variant.longValue==v2.variant.longValue;

			case value::Empty:
			default:
				return true;
		}
	}

	inline
	bool operator!=(value const& v1, value const& v2)
	{
		return !(v1 == v2);
	}
}

#endif /* defined(docopt__value_h_) */
