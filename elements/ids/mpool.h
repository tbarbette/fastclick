/*
 * mpool.c memory pool management
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

#ifndef _MPOOL_H_
#define	_MPOOL_H_
#ifdef	__cplusplus
CLICK_DECLS
extern "C" {
#endif

/* Forward declaration */
struct mpool;


struct mpool *mpool_create (size_t size);
void mpool_free (struct mpool *pool);

void *mpool_malloc (struct mpool *pool, size_t size);
void *mpool_strdup (struct mpool *pool, const char *str);
void *mpool_strndup (struct mpool *pool, const char *str, size_t n);


#ifdef	__cplusplus
}
#endif
CLICK_ENDDECLS
#endif	/* _MPOOL_H_ */
