
// (c) 2009 by Anton Korenyushkin

/// \file common.h
/// Common definitions

#ifndef COMMON_H
#define COMMON_H

#include "type.h"

#include <string>
#include <vector>
#include <map>

namespace ku
{
    typedef orset<std::string> StringSet;
    typedef std::vector<std::string> Strings;
    typedef std::vector<StringSet> StringSets;
    typedef std::vector<Value> Values;
    typedef std::vector<Type> Types;
    typedef std::map<std::string, std::string> StringMap;
    typedef std::map<std::string, Value> ValueMap;
    typedef std::vector<char> Chars;
}

#endif // COMMON_H
