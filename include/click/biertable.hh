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

    bool lookup(const bfrid &dst, bitstring &fbm, IP6Address &nxt, int &index, String &ifname) const;

    void del(const bfrid &dst);
    void add(const bfrid &dst, const bitstring fbm, const IP6Address &nxt, int output, String ifname);
    void clear() { _v.clear(); }
    String dump();

  private:
    struct Entry {
      bfrid _dst;
      bitstring _fbm;
      IP6Address _nxt;
      int _if_idx;
      String _if_name;
      int _valid;
    };

    Vector<Entry> _v;
};

CLICK_ENDDECLS
#endif
