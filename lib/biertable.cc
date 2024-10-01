#include <click/config.h>
#include <click/biertable.hh>
#include <click/straccum.hh>
#include <iostream>
CLICK_DECLS

BierTable::BierTable(){}
BierTable::~BierTable(){}

bool BierTable::lookup(const bfrid &dst) const {
  return false;
}

void BierTable::del(const bfrid &dst) {
  for (int i = 0; i < _v.size(); i++) {
    if (_v[i]._valid && _v[i]._dst == dst)
      _v[i]._valid = 0;
  }
}

void BierTable::add(const bfrid &dst, const bitstring fbm, const IP6Address &nxt) {
  struct Entry e;
  e._dst = dst;
  e._fbm = fbm;
  e._nxt = nxt;
  e._valid = 1;

  // Avoid duplicate routes
  del(dst);
  
  for (int i = 0; i < _v.size(); i++)
    if (!_v[i]._valid) {
      _v[i] = e;
      return;
    }
  _v.push_back(e);
}

String BierTable::dump() {
  StringAccum sa;
  if (_v.size())
    sa << "# Active routes\n# BFR-id    F-BM    Nexthop\n";
  for (int i=0; i< _v.size(); i++) {
    if (_v[i]._valid) {
      sa << "    " << _v[i]._dst;
      sa << "      " << _v[i]._fbm.unparse();
      sa << "      " << _v[i]._nxt << '\n';
    }
  }
  return sa.take_string();
}

CLICK_ENDDECLS
