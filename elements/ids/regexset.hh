#ifndef CLICK_REGEXSET_H_
#define CLICK_REGEXSET_H_
#include <click/string.hh>
#include <click/packet.hh>
#include <click/vector.hh>
#include <re2/re2.h>
#include <re2/set.h>

CLICK_DECLS
class RegexSet {
	public:
		RegexSet();
		RegexSet(int max_mem);
		~RegexSet();
		int add_pattern(const String& pattern);
		bool compile();
		void reset();
		bool is_open() const;

		int match_first(const char* data, int length) const;
		bool match_any(const char *data, int length) const;
		bool match_all(const char *data, int length) const;
		int match_group(const char *data, int length, Vector<int> &pattern_group_numbers, Vector<int> &group_count) const;
		static const int kDefaultMaxMem = 2<<28;

	private:
		bool _compiled;
		unsigned _npatterns;
		int _max_mem;
		re2::RE2::Set *_compiled_regex;
};

inline int
RegexSet::match_first(const char *data, int length) const {
	std::vector<int> matched_patterns;
	re2::StringPiece string_data(data, length);
	if (!_compiled_regex->Match(string_data, &matched_patterns)) {
		return -1;
	}

	int first_match = matched_patterns[0];
	for (unsigned i=1; i < matched_patterns.size(); ++i) {
		if (matched_patterns[i] < first_match) {
		  first_match = matched_patterns[i];
		}
	}

	return first_match;
}


inline bool
RegexSet::match_any(const char *data, int length) const {
	std::vector<int> matched_patterns;
	re2::StringPiece string_data(data, length);
	return _compiled_regex->Match(string_data, &matched_patterns);
}

inline bool
RegexSet::match_all(const char *data, int length) const {
	std::vector<int> matched_patterns;
	re2::StringPiece string_data(data, length);
	if (!_compiled_regex->Match(string_data, &matched_patterns)) {
		return false;
	}
	return matched_patterns.size() == _npatterns;
}

inline int
RegexSet::match_group(const char *data, int length, Vector<int> &pattern_group_numbers, Vector<int> &group_count) const {
	std::vector<int> matched_patterns;
	re2::StringPiece string_data(data, length);
	if (!_compiled_regex->Match(string_data, &matched_patterns)) {
		return -1;
	}

	Vector<int> matched_group_count(group_count.size(), 0);
	for (unsigned i=0; i < matched_patterns.size(); ++i) {
		int pattern_number = matched_patterns[i];
		int group_number = pattern_group_numbers[pattern_number];
		matched_group_count[group_number]++;
	}

	for (int i=0; i<matched_group_count.size(); ++i) {
		if (matched_group_count[i] && matched_group_count[i] == group_count[i]) {
			return i;
		}
	}

	return -1;
}

CLICK_ENDDECLS
#endif /* CLICK_REGEXSET_H_ */
