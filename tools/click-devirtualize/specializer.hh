#ifndef CLICK_SPECIALIZER_HH
#define CLICK_SPECIALIZER_HH
#include "cxxclass.hh"
class RouterT;
class ElementT;
class ErrorHandler;
class ElementMap;
class Signatures;

String click_to_cxx_name(const String &);
String specialized_click_name(ElementT *);

struct ElementTypeInfo {
  String click_name;
  String cxx_name;
  String header_file;
  String source_directory;
  String found_header_file;
  String includes;
  bool read_source;
  bool wrote_includes;

  ElementTypeInfo();
  void locate_header_file(RouterT *, ErrorHandler *);
};

struct SpecializedClass {
  String old_click_name;
  String click_name;
  String cxx_name;
  CxxClass *cxxc;
  int eindex;

  SpecializedClass() : cxxc(0), eindex(-3)	{ }
  bool special() const				{ return cxxc != 0; }
};

class Specializer { public:

  Specializer(RouterT *, const ElementMap &);

  ElementTypeInfo &type_info(const String &);
  const ElementTypeInfo &type_info(const String &) const;
  ElementTypeInfo &etype_info(int);
  const ElementTypeInfo &etype_info(int) const;
  void add_type_info(const String &click_name, const String &cxx_name,
		     const String &header_file, const String &source_dir);

  void specialize(const Signatures &, ErrorHandler *);
  void fix_elements();

  int nspecials() const				{ return _specials.size(); }
  const SpecializedClass &special(int i) const	{ return _specials[i]; }

    void output(StringAccum& out_header, StringAccum& out_source );
    void output_package(const String &package_name, const String &suffix, StringAccum &, ErrorHandler*);
    void output_new_elementmap(const ElementMap &, ElementMap &, const String &,
			       const String &requirements) const;
  void do_config_replacement();
  void verbose(bool verbose) { _verbose = verbose; };
  void should_replace(bool do_replace) { _do_replace = do_replace; };
  void should_inline(bool do_inline) { _do_inline = do_inline; };
  void make_static(bool do_static) { _do_static = do_static; };
  void should_unroll(bool do_unroll, int unroll_val) { _do_unroll = do_unroll; _unroll_val = unroll_val; };
  void should_switch(bool do_switch, int switch_burst) { _do_switch = do_switch; _switch_burst = switch_burst; };
  void should_jmps(bool do_jmps, int jmp_burst) { _do_jmps = do_jmps; _jmp_burst = jmp_burst; };
  void should_align(int do_align) { _do_align = do_align; };
 private:

  enum { SPCE_NOT_DONE = -2, SPCE_NOT_SPECIAL = -1 };

  RouterT *_router;
  int _nelements;
  Vector<int> _ninputs;
  Vector<int> _noutputs;
  Vector<int> _specialize;

  HashTable<String, int> _etinfo_map;
  Vector<ElementTypeInfo> _etinfo;
  HashTable<String, int> _header_file_map;
  HashTable<String, int> _parsed_sources;

  //List of specialized class, populated by specialize()
  Vector<SpecializedClass> _specials;

  bool _do_replace;
  bool _do_inline;
  bool _do_static;
  bool _do_unroll;
  bool _do_switch;
  bool _verbose;
  int _unroll_val;
  int _switch_burst;
  int _do_jmps;
  int _jmp_burst;
  int _do_align;

  CxxInfo _cxxinfo;

  const String &enew_cxx_type(int) const;

  void parse_source_file(ElementTypeInfo &, bool, String *);
  void read_source(ElementTypeInfo &, ErrorHandler *);
  void check_specialize(int, ErrorHandler *);
  bool create_class(SpecializedClass &);
  void do_simple_action(SpecializedClass &);
  void unroll_run_task(SpecializedClass &);
  void switch_run_task(SpecializedClass &);
  void computed_jmps_run_task(SpecializedClass &);
  void create_connector_methods(SpecializedClass &);

  void output_includes(ElementTypeInfo &, StringAccum &);

};

inline
ElementTypeInfo::ElementTypeInfo()
  : read_source(false), wrote_includes(false)
{
}

inline ElementTypeInfo &
Specializer::type_info(const String &name)
{
    return _etinfo[_etinfo_map.get(name)];
}

inline const ElementTypeInfo &
Specializer::type_info(const String &name) const
{
    return _etinfo[_etinfo_map.get(name)];
}

#endif
