/*
 * replace.h: Defines replacement related data structures
 *
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

#ifndef _MF_REPLACE_H_
#define	_MF_REPLACE_H_
#include "actypes.h"
CLICK_DECLS
#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Different replace modes
 */
typedef enum mf_replace_mode
{
    MF_REPLACE_MODE_DEFAULT = 0,
    MF_REPLACE_MODE_NORMAL, /**< Normal replace mode: Short factors are swollen
                              * by the big one; All other patterns are replced
                              * even if they have overlap.
                              */
    MF_REPLACE_MODE_LAZY   /**< Lazy replace mode: every pattern which comes
                             * first is replced; the overlapping pattrns are
                             * nullified by the previous patterns; consequently,
                             * factor patterns nullify the big patterns.
                             */
} MF_REPLACE_MODE_t;


/**
 * Before we replace any pattern we encounter, we should be patient
 * because it may be a factor of another longer pattern. So we maintain a record
 * of each recognized pattern until we make sure that it is not a sub-pattern
 * and can be replaced by its substitute. To keep a record of packets we use
 * the following structure.
 */
struct mf_replacement_nominee
{
    AC_PATTERN_t *pattern;
    size_t position;
};


/**
 * Contains replacement related data
 */
typedef struct mf_replacement_date
{
    AC_TEXT_t buffer;   /**< replacement buffer: maintains the result
                         * of replacement */

    AC_TEXT_t backlog;  /**< replacement backlog: if a pattern is divided
                         * between two or more different chunks, then at the
                         * end of the first chunk we need to keep it here until
                         * the next chunk comes and we decide if it is a
                         * pattern or just a pattern prefix. */

    unsigned int has_replacement; /**< total number of to-be-replaced patterns
                                   */

    struct mf_replacement_nominee *noms; /**< Replacement nominee array */
    size_t noms_capacity; /**< Max capacity of the array */
    size_t noms_size;  /**< Number of nominees in the array */

    size_t curser; /**< the position in the input text before which all
                    * patterns are replaced and the result is saved to the
                    * buffer. */

    MF_REPLACE_MODE_t replace_mode;  /**< Replace mode */

    MF_REPLACE_CALBACK_f cbf;   /**< Callback function */
    void *user;    /**< User parameters sent to the callback function */

    struct ac_trie *trie; /**< Pointer to the trie */

} MF_REPLACEMENT_DATA_t;


#ifdef	__cplusplus
}
#endif
CLICK_ENDDECLS
#endif	/* REPLACE_H */
