#ifndef CLICK_BIERTABLE_HH
#define CLICK_BIERTABLE_HH
#include <click/config.h>
#include <click/confparse.hh>
#include <click/glue.hh>
#include <clicknet/bier.h>
#include <click/ip6address.hh>
CLICK_DECLS

class BierTable {
  public:
    BierTable();
    ~BierTable();

    bool lookup(const bfrid &dst) const;

    void del(const bfrid &dst);
    void add(const bfrid &dst, const bitstring fbm, const IP6Address &nxt);
    void clear() { _v.clear(); }
    String dump();

  private:
    struct Entry {
      bfrid _dst;
      bitstring _fbm;
      IP6Address _nxt;
      int _valid;
      // String 
    };

    Vector<Entry> _v;
};

CLICK_ENDDECLS
#endif
