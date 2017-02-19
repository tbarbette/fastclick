/*
 * node.h: Defines the trie node and interface functions
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

#ifndef _NODE_H_
#define _NODE_H_
#include "actypes.h"
CLICK_DECLS
#ifdef __cplusplus
extern "C" {
#endif

/* Forward Declaration */
struct act_edge;
struct ac_trie;

/**
 * Aho-Corasick Trie node
 */
typedef struct act_node
{
    int id;     /**< Node identifier: used for debugging purpose */

    int final;      /**< A final node accepts pattern; 0: not, 1: is final */
    size_t depth;   /**< Distance between this node and the root */
    struct act_node *failure_node;  /**< The failure transition node */

    struct act_edge *outgoing;  /**< Outgoing edges array */
    size_t outgoing_capacity;   /**< Max capacity of outgoing edges */
    size_t outgoing_size;       /**< Number of outgoing edges */

    AC_PATTERN_t *matched;      /**< Matched patterns array */
    size_t matched_capacity;    /**< Max capacity of the matched patterns */
    size_t matched_size;        /**< Number of matched patterns in this node */

    AC_PATTERN_t *to_be_replaced;   /**< Pointer to the pattern that must be
                                     * replaced */

    struct ac_trie *trie;    /**< The trie that this node belongs to */

} ACT_NODE_t;

/**
 * Edge of the node
 */
struct act_edge
{
    AC_ALPHABET_t alpha;    /**< Transition alpha */
    ACT_NODE_t *next;       /**< Target of the edge */
};

/*
 * Node interface functions
 */

ACT_NODE_t *node_create (struct ac_trie *trie);
ACT_NODE_t *node_create_next (ACT_NODE_t *nod, AC_ALPHABET_t alpha);
ACT_NODE_t *node_find_next (ACT_NODE_t *nod, AC_ALPHABET_t alpha);
ACT_NODE_t *node_find_next_bs (ACT_NODE_t *nod, AC_ALPHABET_t alpha);

void node_assign_id (ACT_NODE_t *nod);
void node_add_edge (ACT_NODE_t *nod, ACT_NODE_t *next, AC_ALPHABET_t alpha);
void node_sort_edges (ACT_NODE_t *nod);
void node_accept_pattern (ACT_NODE_t *nod, AC_PATTERN_t *new_patt, int copy);
void node_collect_matches (ACT_NODE_t *nod);
void node_release_vectors (ACT_NODE_t *nod);
int  node_book_replacement (ACT_NODE_t *nod);
void node_display (ACT_NODE_t *nod);

#ifdef __cplusplus
}
#endif
CLICK_ENDDECLS
#endif
