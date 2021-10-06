#include <click/config.h>
#include "regexset.hh"
CLICK_DECLS

RegexSet::RegexSet() :
_compiled(false),
_npatterns(0)
{
  _max_mem = kDefaultMaxMem;
  re2::RE2::Options options;
  options.set_max_mem(_max_mem);
  _compiled_regex = new re2::RE2::Set(options, re2::RE2::UNANCHORED);
}

RegexSet::RegexSet(int max_mem) :
_compiled(false),
_npatterns(0)
{
  _max_mem = max_mem;
  re2::RE2::Options options;
  options.set_max_mem(_max_mem);
  _compiled_regex = new re2::RE2::Set(options, re2::RE2::UNANCHORED);
}

RegexSet::~RegexSet() {
	if (_compiled_regex) {
		delete _compiled_regex;
	}
}

int
RegexSet::add_pattern(const String& pattern) {
  if (!_compiled_regex) {
	return -2;
  }

  int result = _compiled_regex->Add(re2::StringPiece(pattern.c_str(), pattern.length()), NULL);
  ++_npatterns;
  return result;
}

bool
RegexSet::compile() {
  if (!_compiled_regex) {
	return false;
  }

  _compiled = _compiled_regex->Compile();
  return _compiled;
}

bool
RegexSet::is_open() const {
	return _compiled_regex && !_compiled;
}

void
RegexSet::reset() {
	_compiled = false;
	if (_compiled_regex) {
		delete _compiled_regex;
	}
	re2::RE2::Options options;
	options.set_max_mem(_max_mem);
	_compiled_regex = new re2::RE2::Set(options, re2::RE2::UNANCHORED);
	_npatterns = 0;
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel re2)
ELEMENT_PROVIDES(RegexSet)
ELEMENT_MT_SAFE(RegexSet)
