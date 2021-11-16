/*
 * ahocorasickplus.cc: A sample C++ wrapper for Aho-Corasick C library
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
#include <click/config.h>
#include "ahocorasickplus.hh"
CLICK_DECLS

AhoCorasick::AhoCorasick ()
{
	_automata = ac_trie_create ();
	_text = new AC_TEXT_t;
}

AhoCorasick::~AhoCorasick ()
{
	ac_trie_release (_automata);
	delete _text;
}

AhoCorasick::EnumReturnStatus
AhoCorasick::add_pattern (const String &pattern, PatternId id)
{
	// Adds zero-terminating string

	EnumReturnStatus rv = RETURNSTATUS_FAILED;

	AC_PATTERN_t patt;
	patt.ptext.astring = (AC_ALPHABET_t*) pattern.c_str();
	patt.ptext.length = pattern.length();
	patt.id.u.number = id;
	patt.rtext.astring = NULL;
	patt.rtext.length = 0;

	AC_STATUS_t status = ac_trie_add (_automata, &patt, 0);

	switch (status)
	{
		case ACERR_SUCCESS:
			rv = RETURNSTATUS_SUCCESS;
			break;
		case ACERR_DUPLICATE_PATTERN:
			rv = RETURNSTATUS_DUPLICATE_PATTERN;
			break;
		case ACERR_LONG_PATTERN:
			rv = RETURNSTATUS_LONG_PATTERN;
			break;
		case ACERR_ZERO_PATTERN:
			rv = RETURNSTATUS_ZERO_PATTERN;
			break;
		case ACERR_TRIE_CLOSED:
			rv = RETURNSTATUS_AUTOMATA_CLOSED;
			break;
	}
	return rv;
}

AhoCorasick::EnumReturnStatus
AhoCorasick::add_pattern (const char pattern[], PatternId id)
{
	String tmpString = pattern;
	return add_pattern (tmpString, id);
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel AhoCorasickC)
ELEMENT_PROVIDES(AhoCorasick)
ELEMENT_MT_SAFE(AhoCorasick)
