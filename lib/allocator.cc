#include <click/config.h>
#include <click/allocator.hh>

CLICK_DECLS

bool pool_allocator_mt_base::_dying = false;
int pool_allocator_mt_base::_n_msg = 0;

CLICK_ENDDECLS
