/*
 * actypes.h: Defines basic data types of the trie
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

#ifndef _AC_TYPES_H_
#define _AC_TYPES_H_

#include <stdlib.h>
/* This is a hack for the compiler that for some strange cannot find the
   fallowing defintions in click/config.h
   So we redfine them again here as no op. This is the same definition that
   is used the click/config.h
*/
#ifndef CLICK_DECLS
# define CLICK_DECLS        /* */
# define CLICK_ENDDECLS     /* */
# define CLICK_USING_DECLS  /* */
#endif
#ifndef EXPORT_ELEMENT
# define EXPORT_ELEMENT(x)
# define ELEMENT_REQUIRES(x)
# define ELEMENT_PROVIDES(x)
# define ELEMENT_HEADER(x)
# define ELEMENT_LIBS(x)
# define ELEMENT_MT_SAFE(x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The alphabet type
 *
 * Actually defining AC_ALPHABET_t as a char works for many usage case, but
 * sometimes we deal with streams of other basic types e.g. integers or
 * enumerators. Although they consists of string of bytes (chars), but using
 * their specific types as AC_ALPHABET_t will lead to a better performance.
 * So instead of working with strings of chars, we assume that we are working
 * with strings of AC_ALPHABET_t and leave it optional for users to define
 * their own alphabets.
 */
typedef char AC_ALPHABET_t;

/**
 * The text (strings of alphabets) type that is used for input/output when
 * dealing with the A.C. Trie. The text can contain zero value alphabets.
 */
typedef struct ac_text
{
    const AC_ALPHABET_t *astring;   /**< String of alphabets */
    size_t length;                  /**< String length */
} AC_TEXT_t;

/**
 * Pattern ID type
 * @see struct ac_pattid
 */
enum ac_pattid_type
{
    AC_PATTID_TYPE_DEFAULT = 0,
    AC_PATTID_TYPE_NUMBER,
    AC_PATTID_TYPE_STRING
};

/**
 * Provides a more readable representative for the pattern. Because patterns
 * themselves are not always suitable for displaying (e.g. patterns containing
 * special characters), we offer this type to improve intelligibility of the
 * output. Sometimes it can be also useful, when you are retrieving patterns
 * from a database, to maintain their identifiers in the trie for further
 * reference. We provisioned two possible types as a union. you can add your
 * type here.
 */
typedef struct ac_pattid
{
    union
    {
        const char *stringy;    /**< Null-terminated string */
        long number;            /**< Item indicator */
    } u;

    enum ac_pattid_type type;   /**< Shows the type of id */

} AC_PATTID_t;

/**
 * This is the pattern type that the trie must be fed by.
 */
typedef struct ac_pattern
{
    AC_TEXT_t ptext;    /**< The search string */
    AC_TEXT_t rtext;    /**< The replace string */
    AC_PATTID_t id;   /**< Pattern identifier */
} AC_PATTERN_t;

/**
 * @brief Provides the structure for reporting a match in the text.
 *
 * A match occurs when the trie reaches a final node. Any final
 * node can match one or more patterns at a position in the input text.
 * the 'patterns' field holds these matched patterns. Obviously these
 * matched patterns have same end-position in the text. There is a relationship
 * between matched patterns: the shorter one is a factor (tail) of the longer
 * one. The 'position' maintains the end position of matched patterns.
 */
typedef struct ac_match
{
    AC_PATTERN_t *patterns;     /**< Array of matched pattern(s) */
    size_t size;                /**< Number of matched pattern(s) */

    size_t position;    /**< The end position of the matching pattern(s) in
                         * the input text */
} AC_MATCH_t;

/**
 * The return status of various A.C. Trie functions
 */
typedef enum ac_status
{
    ACERR_SUCCESS = 0,          /**< No error occurred */
    ACERR_DUPLICATE_PATTERN,    /**< Duplicate patterns */
    ACERR_LONG_PATTERN,         /**< Pattern length is too long */
    ACERR_ZERO_PATTERN,         /**< Empty pattern (zero length) */
    ACERR_TRIE_CLOSED       /**< Trie is closed. */
} AC_STATUS_t;

/**
 * @ brief The call-back function to report the matched patterns back to the
 * caller.
 *
 * When a match is found, the trie will reach the caller using this
 * function. You can send parameters to the call-back function when you call
 * _search() or _replace() functions. The call-back function receives those
 * parameters as the second parameter determined by void * in bellow. If you
 * return 0 from call-back function, it will tell trie to continue
 * searching, otherwise it will return from the trie function.
 */
typedef int (*AC_MATCH_CALBACK_f)(AC_MATCH_t *, void *);

/**
 * @brief Call-back function to receive the replacement text (chunk by chunk).
 */
typedef void (*MF_REPLACE_CALBACK_f)(AC_TEXT_t *, void *);

/**
 * Maximum accepted length of search/replace pattern
 */
#define AC_PATTRN_MAX_LENGTH 1024

/**
 * Replacement buffer size
 */
#define MF_REPLACEMENT_BUFFER_SIZE 2048

#if (MF_REPLACEMENT_BUFFER_SIZE <= AC_PATTRN_MAX_LENGTH)
#error "REPLACEMENT_BUFFER_SIZE must be bigger than AC_PATTRN_MAX_LENGTH"
#endif

typedef enum act_working_mode
{
    AC_WORKING_MODE_SEARCH = 0, /* Default */
    AC_WORKING_MODE_FINDNEXT,
    AC_WORKING_MODE_REPLACE     /* Not used */
} ACT_WORKING_MODE_t;


#ifdef __cplusplus
}
#endif

#endif
