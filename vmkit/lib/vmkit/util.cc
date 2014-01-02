#include "vmkit/util.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

using namespace vmkit;

struct Util::char_less_t     Util::char_less;
struct Util::char_less_t_dbg Util::char_less_dbg;

bool Util::char_less_t::operator()(const char* lhs, const char* rhs) const {
	//printf("Compare: %s - %s - %d\n", lhs, rhs, strcmp(lhs, rhs));
	return strcmp(lhs, rhs) < 0; 
}

bool Util::char_less_t_dbg::operator()(const char* lhs, const char* rhs) const {
	printf("Compare: %s - %s - %d\n", lhs, rhs, strcmp(lhs, rhs));
	return strcmp(lhs, rhs) < 0; 
}

