/*
 *
 * This file is part of multifast. Modified to work with click by Pavel Lazar
 *
	Copyright 2010-2015 Kamiar Kanani <kamiar.kanani@gmail.com>

	multifast is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	multifast is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with multifast.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CLICK_AHOCORASICKPLUS_H_
#define CLICK_AHOCORASICKPLUS_H_
#include "ahocorasick.h"
#include <click/string.hh>
#include <click/packet.hh>
CLICK_DECLS

// Forward declarations
struct ac_trie;
struct ac_text;


class AhoCorasick
{
	public:

		enum EnumReturnStatus
		{
			RETURNSTATUS_SUCCESS = 0,       // No error occurred
			RETURNSTATUS_DUPLICATE_PATTERN, // Duplicate patterns
			RETURNSTATUS_LONG_PATTERN,      // Long pattern
			RETURNSTATUS_ZERO_PATTERN,      // Empty pattern (zero length)
			RETURNSTATUS_AUTOMATA_CLOSED,   // Automata is closed
			RETURNSTATUS_FAILED,            // General unknown failure
		};

		typedef unsigned int PatternId;

		struct Match
		{
			unsigned int    position;
			PatternId       id;
		};

	public:

		AhoCorasick();
		~AhoCorasick();

		EnumReturnStatus add_pattern (const String &pattern, PatternId id);
		EnumReturnStatus add_pattern (const char pattern[], PatternId id);
		void finalize();
		void reset();

		bool match_any (const String& text, bool keep);
		bool match_any (const Packet *p, bool keep);
		bool match_any (const char* text, int size, bool keep);
		int match_first(const char* text, int size, bool keep);
		int match_first(const Packet *p, bool keep);
		int match_first(const String& text, bool keep);
		bool is_open();

	private:
		struct ac_trie      *_automata;
		AC_TEXT_t *_text;
};

inline bool
AhoCorasick::match_any (const String& text, bool keep) {
	return match_any(text.c_str(), text.length(), keep);
}

inline bool
AhoCorasick::match_any(const Packet* p, bool keep) {
	return match_any((char *)p->data(), p->length(), keep);
}

inline int
AhoCorasick::match_first(const Packet* p, bool keep) {
	return match_first((char *)p->data(), p->length(), keep);
}

inline int
AhoCorasick::match_first(const String& text, bool keep) {
	return match_first(text.c_str(), text.length(), keep);
}

inline void
AhoCorasick::finalize ()
{
	ac_trie_finalize (_automata);
}

inline bool
AhoCorasick::match_any(const char* text_to_match, int length, bool keep) {
	_text->astring = text_to_match;
	_text->length = length;
	ac_trie_settext(_automata, _text, (int)keep);
	AC_MATCH_t match = ac_trie_findnext(_automata);
	return match.size > 0;
}

inline int
AhoCorasick::match_first(const char* text_to_match, int length, bool keep) {
	_text->astring = text_to_match;
	_text->length = length;
	ac_trie_settext(_automata, _text, (int)keep);
	AC_MATCH_t match = ac_trie_findnext(_automata);
	if (match.size > 0) {
		return (int) match.patterns->id.u.number;
	}
	return -1;
}
inline void
AhoCorasick::reset() {
	ac_trie_release (_automata);
	_automata = ac_trie_create ();
}

inline bool
AhoCorasick::is_open() {
	return _automata->trie_open != 0;
}
CLICK_ENDDECLS
#endif /* CLICK_AHOCORASICKPLUS_H_ */
