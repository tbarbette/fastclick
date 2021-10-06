/*
 * ahocorasick.h: The main ahocorasick header file.
 * This file is part of multifast.
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

#ifndef _AHOCORASICK_H_
#define _AHOCORASICK_H_

#include "replace.h"
CLICK_DECLS
#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct act_node;
struct mpool;

/*
 * The A.C. Trie data structure
 */
typedef struct ac_trie
{
	struct act_node *root;      /**< The root node of the trie */

	size_t patterns_count;      /**< Total patterns in the trie */

	short trie_open; /**< This flag indicates that if trie is finalized
						  * or not. After finalizing the trie you can not
						  * add pattern to trie anymore. */

	struct mpool *mp;   /**< Memory pool */

	/* ******************* Thread specific part ******************** */

	/* It is possible to search a long input chunk by chunk. In order to
	 * connect these chunks and make a continuous view of the input, we need
	 * the following variables.
	 */
	struct act_node *last_node; /**< Last node we stopped at */
	size_t base_position; /**< Represents the position of the current chunk,
						   * related to whole input text */

	AC_TEXT_t *text;    /**< A helper variable to hold the input chunk */
	size_t position;    /**< A helper variable to hold the relative current
						 * position in the given text */

	MF_REPLACEMENT_DATA_t repdata;    /**< Replacement data structure */

	ACT_WORKING_MODE_t wm; /**< Working mode */

} AC_TRIE_t;

/*
 * The API functions
 */

AC_TRIE_t *ac_trie_create (void);
AC_STATUS_t ac_trie_add (AC_TRIE_t *thiz, AC_PATTERN_t *patt, int copy);
void ac_trie_finalize (AC_TRIE_t *thiz);
void ac_trie_release (AC_TRIE_t *thiz);
void ac_trie_display (AC_TRIE_t *thiz);

int  ac_trie_search (AC_TRIE_t *thiz, AC_TEXT_t *text, int keep,
		AC_MATCH_CALBACK_f callback, void *param);

void ac_trie_settext (AC_TRIE_t *thiz, AC_TEXT_t *text, int keep);
AC_MATCH_t ac_trie_findnext (AC_TRIE_t *thiz);

int  multifast_replace (AC_TRIE_t *thiz, AC_TEXT_t *text,
		MF_REPLACE_MODE_t mode, MF_REPLACE_CALBACK_f callback, void *param);
void multifast_rep_flush (AC_TRIE_t *thiz, int keep);


#ifdef __cplusplus
CLICK_ENDDECLS
}
#endif

#endif
