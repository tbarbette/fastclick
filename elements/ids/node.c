/*
 * node.c: Implements the A.C. Trie node
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
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "actypes.h"
#include "node.h"
#include "mpool.h"
#include "ahocorasick.h"
CLICK_DECLS

/* Privates */
static void node_init (ACT_NODE_t *thiz);
static int  node_edge_compare (const void *l, const void *r);
static int  node_has_pattern (ACT_NODE_t *thiz, AC_PATTERN_t *patt);
static void node_grow_outgoing_vector (ACT_NODE_t *thiz);
static void node_grow_matched_vector (ACT_NODE_t *thiz);
static void node_copy_pattern (ACT_NODE_t *thiz,
        AC_PATTERN_t *to, AC_PATTERN_t *from);

/**
 * @brief Creates the node
 *
 * @return
******************************************************************************/
struct act_node * node_create (struct ac_trie *trie)
{
    ACT_NODE_t *node;

    node = (ACT_NODE_t *) mpool_malloc (trie->mp, sizeof(ACT_NODE_t));
    node_init (node);
    node->trie = trie;

    return node;
}

/**
 * @brief Initializes the node
 *
 * @param thiz
 *****************************************************************************/
static void node_init (ACT_NODE_t *thiz)
{
    node_assign_id (thiz);

    thiz->final = 0;
    thiz->failure_node = NULL;
    thiz->depth = 0;

    thiz->matched = NULL;
    thiz->matched_capacity = 0;
    thiz->matched_size = 0;

    thiz->outgoing = NULL;
    thiz->outgoing_capacity = 0;
    thiz->outgoing_size = 0;

    thiz->to_be_replaced = NULL;
}

/**
 * @brief Releases the node memories
 *
 * @param thiz
 *****************************************************************************/
void node_release_vectors(ACT_NODE_t *nod)
{
    free(nod->matched);
    free(nod->outgoing);
}

/**
 * @brief Finds out the next node for a given alpha. this function is used in
 * the pre-processing stage in which edge array is not sorted. so it uses
 * linear search.
 *
 * @param thiz
 * @param alpha
 * @return
 *****************************************************************************/
ACT_NODE_t * node_find_next(ACT_NODE_t *nod, AC_ALPHABET_t alpha)
{
    size_t i;

    for (i=0; i < nod->outgoing_size; i++)
    {
        if(nod->outgoing[i].alpha == alpha)
            return (nod->outgoing[i].next);
    }
    return NULL;
}

/**
 * @brief Finds out the next node for a given alpha. this function is used
 * after the pre-processing stage in which we sort edges. so it uses Binary
 * Search.
 *
 * @param thiz
 * @param alpha
 * @return
 *****************************************************************************/
ACT_NODE_t *node_find_next_bs (ACT_NODE_t *nod, AC_ALPHABET_t alpha)
{
    size_t mid;
    int min, max;
    AC_ALPHABET_t amid;

    min = 0;
    max = nod->outgoing_size - 1;

    while (min <= max)
    {
        mid = (min + max) >> 1;
        amid = nod->outgoing[mid].alpha;
        if (alpha > amid)
            min = mid + 1;
        else if (alpha < amid)
            max = mid - 1;
        else
            return (nod->outgoing[mid].next);
    }
    return NULL;
}

/**
 * @brief Determines if a final node contains a pattern in its accepted pattern
 * list or not.
 *
 * @param thiz
 * @param newstr
 * @return 1: has the pattern, 0: doesn't have it
 *****************************************************************************/
static int node_has_pattern (ACT_NODE_t *thiz, AC_PATTERN_t *patt)
{
    size_t i, j;
    AC_TEXT_t *txt;
    AC_TEXT_t *new_txt = &patt->ptext;

    for (i = 0; i < thiz->matched_size; i++)
    {
        txt = &thiz->matched[i].ptext;

        if (txt->length != new_txt->length)
            continue;

        /* The following loop is futile! Because the input pattern always come
         * from a failure node, and if they have the same length, then they are
         * equal. But for the sake of functional integrity we leave it here. */

        for (j = 0; j < txt->length; j++)
            if (txt->astring[j] != new_txt->astring[j])
                break;

        if (j == txt->length)
            return 1;
    }
    return 0;
}

/**
 * @brief Create the next node for the given alpha.
 *
 * @param thiz
 * @param alpha
 * @return
 *****************************************************************************/
ACT_NODE_t *node_create_next (ACT_NODE_t *nod, AC_ALPHABET_t alpha)
{
    ACT_NODE_t *next;

    if (node_find_next (nod, alpha) != NULL)
        /* The edge already exists */
        return NULL;

    next = node_create (nod->trie);
    node_add_edge (nod, next, alpha);

    return next;
}

/**
 * @brief Adds the pattern to the list of accepted pattern.
 *
 * @param thiz
 * @param str
 * @param copy
 *****************************************************************************/
void node_accept_pattern (ACT_NODE_t *nod, AC_PATTERN_t *new_patt, int copy)
{
    AC_PATTERN_t *patt;

    /* Check if the new pattern already exists in the node list */
    if (node_has_pattern(nod, new_patt))
        return;

    /* Manage memory */
    if (nod->matched_size == nod->matched_capacity)
        node_grow_matched_vector (nod);

    patt = &nod->matched[nod->matched_size++];

    if (copy)
    {
        /* Deep copy */
        node_copy_pattern (nod, patt, new_patt);
    }
    else
    {
        /* Shallow copy */
        *patt = *new_patt;
    }
}

/**
 * @brief Makes a deep copy of the pattern
 *
 * @param thiz pointer to the owner node
 * @param from
 * @param to
 *****************************************************************************/
static void node_copy_pattern
    (ACT_NODE_t *thiz, AC_PATTERN_t *to, AC_PATTERN_t *from)
{
    struct mpool *mp = thiz->trie->mp;

    to->ptext.astring = (AC_ALPHABET_t *) mpool_strndup (mp,
        (const char *) from->ptext.astring,
        from->ptext.length * sizeof(AC_ALPHABET_t));
    to->ptext.length = from->ptext.length;

    to->rtext.astring = (AC_ALPHABET_t *) mpool_strndup (mp,
        (const char *) from->rtext.astring,
        from->rtext.length * sizeof(AC_ALPHABET_t));
    to->rtext.length = from->rtext.length;

    if (from->id.type == AC_PATTID_TYPE_STRING)
        to->id.u.stringy = (const char *) mpool_strdup (mp,
                (const char *) from->id.u.stringy);
    else
        to->id.u.number = from->id.u.number;

    to->id.type = from->id.type;
}

/**
 * @brief Establish an edge between two nodes
 *
 * @param thiz
 * @param next
 * @param alpha
 *****************************************************************************/
void node_add_edge (ACT_NODE_t *nod, ACT_NODE_t *next, AC_ALPHABET_t alpha)
{
    struct act_edge *oe; /* Outgoing edge */

    if(nod->outgoing_size == nod->outgoing_capacity)
        node_grow_outgoing_vector (nod);

    oe = &nod->outgoing[nod->outgoing_size];
    oe->alpha = alpha;
    oe->next = next;
    nod->outgoing_size++;
}

/**
 * @brief Assigns a unique ID to the node (used for debugging purpose)
 *
 * @param thiz
 *****************************************************************************/
void node_assign_id (ACT_NODE_t *nod)
{
    static int unique_id = 1;
    nod->id = unique_id++;
}

/**
 * @brief Comparison function for qsort. see man qsort.
 *
 * @param l left side
 * @param r right side
 * @return According to the man page: The comparison function must return an
 * integer less than, equal to, or greater than zero if the first argument is
 * considered to be respectively less than, equal to, or greater than the
 * second. if two members compare as equal, their order in the sorted array is
 * undefined.
 *****************************************************************************/
static int node_edge_compare (const void *l, const void *r)
{
    /*
     * NOTE: Because edge alphabets are unique in every node we ignore
     * equivalence case.
     */
    if (((struct act_edge *)l)->alpha >= ((struct act_edge *)r)->alpha)
        return 1;
    else
        return -1;
}

/**
 * @brief Sorts edges alphabets.
 *
 * @param thiz
 *****************************************************************************/
void node_sort_edges (ACT_NODE_t *nod)
{
    qsort ((void *)nod->outgoing, nod->outgoing_size,
            sizeof(struct act_edge), node_edge_compare);
}

/**
 * @brief Bookmarks the to-be-replaced patterns
 *
 * If there was more than one pattern accepted in a node then only one of them
 * must be replaced: The longest pattern that has a requested replacement.
 *
 * @param node
 * @return 1 if there was any replacement, 0 otherwise
 *****************************************************************************/
int node_book_replacement (ACT_NODE_t *nod)
{
    size_t j;
    AC_PATTERN_t *pattern;
    AC_PATTERN_t *longest = NULL;

    if(!nod->final)
        return 0;

    for (j=0; j < nod->matched_size; j++)
    {
        pattern = &nod->matched[j];

        if (pattern->rtext.astring != NULL)
        {
            if (!longest)
                longest = pattern;
            else if (pattern->ptext.length > longest->ptext.length)
                longest = pattern;
        }
    }

    nod->to_be_replaced = longest;

    return longest ? 1 : 0;
}

/**
 * @brief Grows the size of outgoing edges vector
 *
 * @param thiz
 *****************************************************************************/
static void node_grow_outgoing_vector (ACT_NODE_t *thiz)
{
    const size_t grow_factor = (8 / (thiz->depth + 1)) + 1;

    /* The outgoing edges of nodes grow with different pace in different
     * depths; the shallower nodes the bigger outgoing number of nodes.
     * So for efficiency (speed & memory usage), we apply a measure to
     * manage different growth rate.
     */

    if (thiz->outgoing_capacity == 0)
    {
        thiz->outgoing_capacity = grow_factor;
        thiz->outgoing = (struct act_edge *) malloc
                (thiz->outgoing_capacity * sizeof(struct act_edge));
    }
    else
    {
        thiz->outgoing_capacity += grow_factor;
        thiz->outgoing = (struct act_edge *) realloc (
                thiz->outgoing,
                thiz->outgoing_capacity * sizeof(struct act_edge));
    }
}

/**
 * @brief Grows the size of matched patterns vector
 *
 * @param thiz
 *****************************************************************************/
static void node_grow_matched_vector (ACT_NODE_t *thiz)
{
    if (thiz->matched_capacity == 0)
    {
        thiz->matched_capacity = 1;
        thiz->matched = (AC_PATTERN_t *) malloc
                (thiz->matched_capacity * sizeof(AC_PATTERN_t));
    }
    else
    {
        thiz->matched_capacity += 2;
        thiz->matched = (AC_PATTERN_t *) realloc (
                thiz->matched,
                thiz->matched_capacity * sizeof(AC_PATTERN_t));
    }
}

/**
 * @brief Collect accepted patterns of the node.
 *
 * The accepted patterns consist of the node's own accepted pattern plus
 * accepted patterns of its failure node.
 *
 * @param node
 *****************************************************************************/
void node_collect_matches (ACT_NODE_t *nod)
{
    size_t i;
    ACT_NODE_t *n = nod;

    while ((n = n->failure_node))
    {
        for (i = 0; i < n->matched_size; i++)
            /* Always call with copy parameter 0 */
            node_accept_pattern (nod, &(n->matched[i]), 0);

        if (n->final)
            nod->final = 1;
    }

    node_sort_edges (nod);
    /* Sort matched patterns? Is that necessary? I don't think so. */
}

/**
 * @brief Displays all nodes recursively
 *
 * @param n
 * @param repcast
 *****************************************************************************/
void node_display (ACT_NODE_t *nod)
{
    size_t j;
    struct act_edge *e;
    AC_PATTERN_t patt;

    printf("NODE(%3d)/....fail....> ", nod->id);
    if (nod->failure_node)
        printf("NODE(%3d)\n", nod->failure_node->id);
    else
        printf ("N.A.\n");

    for (j = 0; j < nod->outgoing_size; j++)
    {
        e = &nod->outgoing[j];
        printf("         |----(");
        if(isgraph(e->alpha))
            printf("%c)---", e->alpha);
        else
            printf("0x%x)", e->alpha);
        printf("--> NODE(%3d)\n", e->next->id);
    }

    if (nod->matched_size)
    {
        printf("Accepts: {");
        for (j = 0; j < nod->matched_size; j++)
        {
            patt = nod->matched[j];
            if(j)
                printf(", ");
            switch (patt.id.type)
            {
            case AC_PATTID_TYPE_DEFAULT:
            case AC_PATTID_TYPE_NUMBER:
                printf("%ld", patt.id.u.number);
                break;
            case AC_PATTID_TYPE_STRING:
                printf("%s", patt.id.u.stringy);
                break;
            }
            printf(": %.*s", (int)patt.ptext.length, patt.ptext.astring);
        }
        printf("}\n");
    }
    printf("\n");
}

CLICK_ENDDECLS
ELEMENT_PROVIDES(AhoCorasickNodeC)
