/*
 * ahocorasick.c: Implements the A. C. Trie functionalities
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
#include <click/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actypes.h"
#include "mpool.h"
#include "node.h"
#include "ahocorasick.h"

CLICK_DECLS

/* Privates */

static void ac_trie_set_failure
	(ACT_NODE_t *node, AC_ALPHABET_t *alphas);

static void ac_trie_traverse_setfailure
	(ACT_NODE_t *node, AC_ALPHABET_t *prefix);

static void ac_trie_traverse_action
	(ACT_NODE_t *node, void(*func)(ACT_NODE_t *), int top_down);

static void ac_trie_reset
	(AC_TRIE_t *thiz);

static int ac_trie_match_handler
	(AC_MATCH_t * matchp, void * param);

/* Friends */

extern void mf_repdata_init (AC_TRIE_t *thiz);
extern void mf_repdata_reset (MF_REPLACEMENT_DATA_t *rd);
extern void mf_repdata_release (MF_REPLACEMENT_DATA_t *rd);
extern void mf_repdata_allocbuf (MF_REPLACEMENT_DATA_t *rd);


/**
 * @brief Initializes the trie; allocates memories and sets initial values
 *
 * @return
 *****************************************************************************/
AC_TRIE_t *ac_trie_create (void)
{
	AC_TRIE_t *thiz = (AC_TRIE_t *) malloc (sizeof(AC_TRIE_t));
	thiz->mp = mpool_create(0);

	thiz->root = node_create (thiz);

	thiz->patterns_count = 0;

	mf_repdata_init (thiz);
	ac_trie_reset (thiz);
	thiz->text = NULL;
	thiz->position = 0;

	thiz->wm = AC_WORKING_MODE_SEARCH;
	thiz->trie_open = 1;

	return thiz;
}

/**
 * @brief Adds pattern to the trie.
 *
 * @param Thiz pointer to the trie
 * @param Patt pointer to the pattern
 * @param copy should trie make a copy of patten strings or not, if not,
 * then user must keep the strings valid for the life-time of the trie. If
 * the pattern are available in the user program then call the function with
 * copy = 0 and do not waste memory.
 *
 * @return The return value indicates the success or failure of adding action
 *****************************************************************************/
AC_STATUS_t ac_trie_add (AC_TRIE_t *thiz, AC_PATTERN_t *patt, int copy)
{
	size_t i;
	ACT_NODE_t *n = thiz->root;
	ACT_NODE_t *next;
	AC_ALPHABET_t alpha;

	if(!thiz->trie_open)
		return ACERR_TRIE_CLOSED;

	if (!patt->ptext.length)
		return ACERR_ZERO_PATTERN;

	if (patt->ptext.length > AC_PATTRN_MAX_LENGTH)
		return ACERR_LONG_PATTERN;

	for (i = 0; i < patt->ptext.length; i++)
	{
		alpha = patt->ptext.astring[i];
		if ((next = node_find_next (n, alpha)))
		{
			n = next;
			continue;
		}
		else
		{
			next = node_create_next (n, alpha);
			next->depth = n->depth + 1;
			n = next;
		}
	}

	if(n->final)
		return ACERR_DUPLICATE_PATTERN;

	n->final = 1;
	node_accept_pattern (n, patt, copy);
	thiz->patterns_count++;

	return ACERR_SUCCESS;
}

/**
 * @brief Finalizes the preprocessing stage and gets the trie ready
 *
 * Locates the failure node for all nodes and collects all matched
 * pattern for each node. It also sorts outgoing edges of node, so binary
 * search could be performed on them. After calling this function the automate
 * will be finalized and you can not add new patterns to the automate.
 *
 * @param thiz pointer to the trie
 *****************************************************************************/
void ac_trie_finalize (AC_TRIE_t *thiz)
{
	AC_ALPHABET_t prefix[AC_PATTRN_MAX_LENGTH];

	/* 'prefix' defined here, because ac_trie_traverse_setfailure() calls
	 * itself recursively */
	ac_trie_traverse_setfailure (thiz->root, prefix);

	ac_trie_traverse_action (thiz->root, node_collect_matches, 1);
	mf_repdata_allocbuf (&thiz->repdata);

	thiz->trie_open = 0; /* Do not accept patterns any more */
}

/**
 * @brief Search in the input text using the given trie.
 *
 * @param thiz pointer to the trie
 * @param text input text to be searched
 * @param keep indicated that if the input text the successive chunk of the
 * previous given text or not
 * @param callback when a match occurs this function will be called. The
 * call-back function in turn after doing its job, will return an integer
 * value, 0 means continue search, and non-0 value means stop search and return
 * to the caller.
 * @param user this parameter will be send to the call-back function
 *
 * @return
 * -1:  failed; trie is not finalized
 *  0:  success; input text was searched to the end
 *  1:  success; input text was searched partially. (callback broke the loop)
 *****************************************************************************/
int ac_trie_search (AC_TRIE_t *thiz, AC_TEXT_t *text, int keep,
		AC_MATCH_CALBACK_f callback, void *user)
{
	size_t position;
	ACT_NODE_t *current;
	ACT_NODE_t *next;
	AC_MATCH_t match;

	if (thiz->trie_open)
		return -1;  /* Trie must be finalized first. */

	if (thiz->wm == AC_WORKING_MODE_FINDNEXT)
		position = thiz->position;
	else
		position = 0;

	current = thiz->last_node;

	if (!keep)
		ac_trie_reset (thiz);

	/* This is the main search loop.
	 * It must be kept as lightweight as possible.
	 */
	while (position < text->length)
	{
		if (!(next = node_find_next_bs (current, text->astring[position])))
		{
			if(current->failure_node /* We are not in the root node */)
				current = current->failure_node;
			else
				position++;
		}
		else
		{
			current = next;
			position++;
		}

		if (current->final && next)
		/* We check 'next' to find out if we have come here after a alphabet
		 * transition or due to a fail transition. in second case we should not
		 * report match, because it has already been reported */
		{
			/* Found a match! */
			match.position = position + thiz->base_position;
			match.size = current->matched_size;
			match.patterns = current->matched;

			/* Do call-back */
			if (callback(&match, user))
			{
				if (thiz->wm == AC_WORKING_MODE_FINDNEXT) {
					thiz->position = position;
					thiz->last_node = current;
				}
				return 1;
			}
		}
	}

	/* Save status variables */
	thiz->last_node = current;
	thiz->base_position += position;

	return 0;
}

/**
 * @brief sets the input text to be searched by a function call to _findnext()
 *
 * @param thiz The pointer to the trie
 * @param text The text to be searched. The owner of the text is the
 * calling program and no local copy is made, so it must be valid until you
 * have done with it.
 * @param keep Indicates that if the given text is the sequel of the previous
 * one or not; 1: it is, 0: it is not
 *****************************************************************************/
void ac_trie_settext (AC_TRIE_t *thiz, AC_TEXT_t *text, int keep)
{
	if (!keep)
		ac_trie_reset (thiz);

	thiz->text = text;
	thiz->position = 0;
}

/**
 * @brief finds the next match in the input text which is set by _settext()
 *
 * @param thiz The pointer to the trie
 * @return A pointer to the matched structure
 *****************************************************************************/
AC_MATCH_t ac_trie_findnext (AC_TRIE_t *thiz)
{
	AC_MATCH_t match;

	thiz->wm = AC_WORKING_MODE_FINDNEXT;
	match.size = 0;

	ac_trie_search (thiz, thiz->text, 1,
			ac_trie_match_handler, (void *)&match);

	thiz->wm = AC_WORKING_MODE_SEARCH;

	return match;
}

/**
 * @brief Release all allocated memories to the trie
 *
 * @param thiz pointer to the trie
 *****************************************************************************/
void ac_trie_release (AC_TRIE_t *thiz)
{
	/* It must be called with a 0 top-down parameter */
	ac_trie_traverse_action (thiz->root, node_release_vectors, 0);

	mf_repdata_release (&thiz->repdata);
	mpool_free(thiz->mp);
	free(thiz);
}

/**
 * @brief Prints the trie to output in human readable form. It is useful
 * for debugging purpose.
 *
 * @param thiz pointer to the trie
 *****************************************************************************/
void ac_trie_display (AC_TRIE_t *thiz)
{
	ac_trie_traverse_action (thiz->root, node_display, 1);
}

/**
 * @brief the match handler function used in _findnext function
 *
 * @param matchp
 * @param param
 * @return
 *****************************************************************************/
static int ac_trie_match_handler (AC_MATCH_t * matchp, void * param)
{
	AC_MATCH_t * mp = (AC_MATCH_t *)param;
	mp->position = matchp->position;
	mp->patterns = matchp->patterns;
	mp->size = matchp->size;
	return 1;
}

/**
 * @brief reset the trie and make it ready for doing new search
 *
 * @param thiz pointer to the trie
 *****************************************************************************/
static void ac_trie_reset (AC_TRIE_t *thiz)
{
	thiz->last_node = thiz->root;
	thiz->base_position = 0;
	mf_repdata_reset (&thiz->repdata);
}

/**
 * @brief Finds and bookmarks the failure transition for the given node.
 *
 * @param node the node pointer
 * @param prefix The array that contain the prefix that leads the path from
 * root the the node.
 *****************************************************************************/
static void ac_trie_set_failure
	(ACT_NODE_t *node, AC_ALPHABET_t *prefix)
{
	size_t i, j;
	ACT_NODE_t *n;
	ACT_NODE_t *root = node->trie->root;

	if (node == root)
		return; /* Failure transition is not defined for the root */

	for (i = 1; i < node->depth; i++)
	{
		n = root;
		for (j = i; j < node->depth && n; j++)
			n = node_find_next (n, prefix[j]);
		if (n)
		{
			node->failure_node = n;
			break;
		}
	}

	if (!node->failure_node)
		node->failure_node = root;
}

/**
 * @brief Sets the failure transition node for all nodes
 *
 * Traverse all trie nodes using DFS (Depth First Search), meanwhile it set
 * the failure node for every node it passes through. this function is called
 * after adding last pattern to trie.
 *
 * @param node The pointer to the root node
 * @param prefix The array that contain the prefix that leads the path from
 * root the the node
 *****************************************************************************/
static void ac_trie_traverse_setfailure
	(ACT_NODE_t *node, AC_ALPHABET_t *prefix)
{
	size_t i;

	/* In each node, look for its failure node */
	ac_trie_set_failure (node, prefix);

	for (i = 0; i < node->outgoing_size; i++)
	{
		prefix[node->depth] = node->outgoing[i].alpha; /* Make the prefix */

		/* Recursively call itself to traverse all nodes */
		ac_trie_traverse_setfailure (node->outgoing[i].next, prefix);
	}
}

/**
 * @brief Traverses the trie using DFS method and applies the
 * given @param func on all nodes. At top level it should be called by
 * sending the the root node.
 *
 * @param node Pointer to trie root node
 * @param func The function that must be applied to all nodes
 * @param top_down Indicates that if the action should be applied to the note
 * itself and then to its children or vise versa.
 *****************************************************************************/
static void ac_trie_traverse_action
	(ACT_NODE_t *node, void(*func)(ACT_NODE_t *), int top_down)
{
	size_t i;

	if (top_down)
		func (node);

	for (i = 0; i < node->outgoing_size; i++)
		/* Recursively call itself to traverse all nodes */
		ac_trie_traverse_action (node->outgoing[i].next, func, top_down);

	if (!top_down)
		func (node);
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(AhoCorasickC)
