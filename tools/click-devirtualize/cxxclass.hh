#ifndef CXXCLASS_HH
#define CXXCLASS_HH
#include <click/string.hh>
#include <click/vector.hh>
#include <click/hashtable.hh>
class StringAccum;
class DevirtualizeTest;

String compile_pattern(const String &);

class CxxFunction {

  String _name;
  bool _in_header;
  bool _from_header_file;
  bool _alive;
  String _ret_type;
  String _args;
  String _body;
  String _clean_body;

  bool find_expr(const String &, int *, int *, int[10], int[10], bool allow_call = false, bool full_symbol = false, int start_at = 0, int stop_at = -1) const;

 public:

  static bool parsing_header_file;

  CxxFunction()				: _alive(false) { }
  CxxFunction(const String &, bool, const String &, const String &,
	      const String &, const String &);

  String name() const			{ return _name; }
  bool alive() const			{ return _alive; }
  bool in_header() const		{ return _in_header; }
  bool from_header_file() const		{ return _from_header_file; }
  const String &ret_type() const	{ return _ret_type; }
  const String &args() const		{ return _args; }
  const String &body() const		{ return _body; }
  const String &clean_body() const	{ return _clean_body; }

  void set_body(const String &b)	{ _body = b; _clean_body = String(); }
  void kill()				{ _alive = false; }
  void unkill()				{ _alive = true; }

    String find_assignment(const String symbol, int stop_at);

  bool find_expr(const String &) const;
  bool replace_expr(const String &, const String &, bool full_symbol = false, bool all = false, int start_at = 0);
  int replace_call(const String &, const String &, Vector<String>& args);

  void set_inline();

};

class CxxClass;
struct ParentalLink {
    ParentalLink() : parent(0), template_params() {
    }
    CxxClass *  parent;
    String      template_params;
};

class CxxClass {

  String _name;
  Vector<ParentalLink> _parents;
  String _template;

  HashTable<String, int> _fn_map;
  Vector<CxxFunction> _functions;
  Vector<int> _has_push;
  Vector<int> _has_pull;
  Vector<int> _should_rewrite;

  bool reach(int, Vector<int> &);

 public:

  CxxClass(const String &);

  const String &name() const		{ return _name; }
  int nparents() const			{ return _parents.size(); }
  CxxClass *parent(int i) const		{ return _parents[i].parent; }
  String parent_tmpl(int i) const		{ return _parents[i].template_params; }
  int nfunctions() const		{ return _functions.size(); }
  CxxFunction *find(const String &);
  CxxFunction *find_in_parent(const String &, const String &);
  CxxFunction &function(int i)		{ return _functions[i]; }

  CxxFunction &defun(const CxxFunction &, const bool &rewrite = false);
  void add_parent(CxxClass *, const String str = "");

  void set_template(String tmpl) {
    _template = tmpl;
  }


  enum RewriteStatus {REWRITE_NEVER, REWRITE_NO, REWRITE_YES};

  RewriteStatus find_should_rewrite();
  bool should_rewrite(int i) const	{ return _should_rewrite[i]; }

  void header_text(StringAccum &, int) const;
  void source_text(StringAccum &) const;

  void print_function_list();

  friend class DevirtualizeTest;
};

class CxxInfo { public:

  CxxInfo();
  ~CxxInfo();

  void parse_file(const String &, bool header, String * = 0);

  CxxClass *find_class(const String &) const;
  CxxClass *make_class(const String &);

 private:

  HashTable<String, int> _class_map;
  Vector<CxxClass *> _classes;

  CxxInfo(const CxxInfo &);
  CxxInfo &operator=(const CxxInfo &);

  int parse_function_definition(const String &text, int fn_start_p,
				int paren_p, const String &original,
				CxxClass *cxx_class);
  int parse_class_definition(const String &, int, const String &, CxxClass* &);
  int parse_class(const String &text, int p, const String &original,
		  CxxClass *cxx_class);

  friend class DevirtualizeTest;

};


inline CxxFunction *
CxxClass::find(const String &name)
{
    int which = _fn_map.get(name);
    return (which >= 0 ? &_functions[which] : 0);
}

inline CxxClass *
CxxInfo::find_class(const String &name) const
{
    int which = _class_map.get(name);
    return (which >= 0 ? _classes[which] : 0);
}

#endif
