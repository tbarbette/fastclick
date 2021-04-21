/*
 * cxxclass.{cc,hh} -- representation of C++ classes
 * Eddie Kohler
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>

#include "cxxclass.hh"
#include <click/straccum.hh>

bool CxxFunction::parsing_header_file;

CxxFunction::CxxFunction(const String &name, bool in_header,
             const String &ret_type, const String &args,
             const String &body, const String &clean_body)
  : _name(name), _in_header(in_header), _from_header_file(parsing_header_file),
    _alive(true), _ret_type(ret_type), _args(args),
    _body(body), _clean_body(clean_body)
{
  //fprintf(stderr, "%s::%s\n", _name.c_str(), _body.c_str());
}

void CxxFunction::set_inline()
{
    if (_ret_type.find_left("inline"))
        _ret_type = "inline "+_ret_type;
}

String
compile_pattern(const String &pattern0)
{
  static const char *three_tokens[] = { ">>=", "<<=", "->*", "::*", 0 };
  static const char *two_tokens[] = { "++", "--", "+=", "-=", "*=", "/=", "->",
                      "%=", "^=", "&=", "~=", "==", "!=",
                      "&&", "||",
                      ">=", "<=", "::", "<<", ">>", ".*", 0 };

  StringAccum sa;
  const char *s = pattern0.data();
  const char *end_s = s + pattern0.length();

  while (s < end_s && isspace((unsigned char) *s)) // skip leading space
    s++;

  // XXX not all constraints on patterns are expressed here
  while (s < end_s) {
    if (isspace((unsigned char) *s)) {
      sa << ' ';
      while (s < end_s && isspace((unsigned char) *s))
    s++;

    } else if (isalnum((unsigned char) *s) || *s == '_') {
      while (s < end_s && (isalnum((unsigned char) *s) || *s == '_'))
    sa << *s++;
      sa << ' ';

    } else if (*s == '#') {
      assert(s < end_s - 1 && isdigit((unsigned char) s[1]));
      sa << s[0] << s[1];
      s += 2;

    } else {
      const char *token = 0;
      if (s < end_s - 2)
    for (int i = 0; !token && three_tokens[i]; i++)
      if (strncmp(three_tokens[i], s, 3) == 0)
        token = three_tokens[i];
      if (!token && s < end_s - 1)
    for (int i = 0; !token && two_tokens[i]; i++)
      if (strncmp(two_tokens[i], s, 2) == 0)
        token = two_tokens[i];
      if (!token)
    sa << *s++ << ' ';
      else {
    sa << token << ' ';
    s += strlen(token);
      }
    }
  }

  return sa.take_string();
}

/**
 * Find an expression in the function
 * @args pattern A symbol to search for
 * @args pos1 Start position of the pattern if found
 * @args pos2 End position of the pattern if found
 * @args allow_call Allow calls to functions
 * @args full_symbol Only match if pattern is the full symbol, eg "n" will not
 *       be a match in "_n"
 *
 */
bool
CxxFunction::find_expr(const String &pattern, int *pos1, int *pos2,
               int match_pos[10], int match_len[10], bool allow_call, bool full_symbol, int start_at, int stop_at) const
{
  const char *ps = pattern.data();
  int plen = pattern.length();

  const char *ts = _clean_body.data();
  int tpos = start_at;
  int tlen = _clean_body.length();
  if (stop_at == -1)
      stop_at = tlen;
  while (tpos < stop_at) {

    // fast loop: look for occurrences of first character in pattern
    while (tpos < stop_at && ts[tpos] != ps[0])
      tpos++;

    int tpos1 = tpos;
    tpos++;
    //position in pattern
    int ppos = 1;

    while (tpos < stop_at && ppos < plen) {

      if (isspace((unsigned char) ps[ppos])) {
          if (ppos > 0 && (isalnum((unsigned char) ps[ppos-1]) || ps[ppos-1] == '_')
              && (isalnum((unsigned char) ts[tpos]) || ts[tpos] == '_'))
          break;
          while (tpos < stop_at && isspace((unsigned char) ts[tpos])) {
              tpos++;
          }
          ppos++;
      } else if (ps[ppos] == '#') {
        // save expr and skip over it
        int paren_level = 0;
        int question_level = 0;
        int which = ps[ppos+1] - '0';
        match_pos[which] = tpos;
        while (tpos < stop_at) {
          if (ts[tpos] == '(')
            paren_level++;
          else if (ts[tpos] == ')') {
            if (paren_level == 0)
              break;
            paren_level--;
          } else if (ts[tpos] == ',') {
            if (paren_level == 0 && question_level == 0)
              break;
          } else if (ts[tpos] == '?')
            question_level++;
          else if (ts[tpos] == ':' && question_level)
            question_level--;
          tpos++;
        }
        match_len[which] = tpos - match_pos[which];
        ppos += 2;
      } else if (ps[ppos] == ts[tpos])
        ppos++, tpos++;
      else
        break;
    }

    if (ppos >= plen) {
      // if full_symbol, check that the pattern was complete
      if (full_symbol) {
        if (tpos < stop_at && !(isspace(ts[tpos]) || (allow_call && ts[tpos] == '(') || ts[tpos] == ';' || ts[tpos] == ')')) {
          continue;
        }
      }

      // check that this pattern match didn't occur after some evil qualifier,
      // namely '.', '::', or '->'
      int p = tpos1 - 1;
      while (p >= 0 && isspace((unsigned char) ts[p]))
         p--;
      if (full_symbol && (isalnum(ts[p]) || (ts[p] == '_')))
        continue;
      if (allow_call || p < 0
      || (ts[p] != '.'
          && (p == 0 || ts[p-1] != ':' || ts[p] != ':')
          && (p == 0 || ts[p-1] != '-' || ts[p] != '>'))) {
        *pos1 = tpos1;
        *pos2 = tpos;
        return true;
      }
    }

    // look for next match
    tpos = tpos1 + 1;
  }

  return false;
}

bool
CxxFunction::find_expr(const String &pattern) const
{
  int pos1, pos2, match_pos[10], match_len[10];
  return find_expr(pattern, &pos1, &pos2, match_pos, match_len);
}

enum expr_type_t {EX_ASSIGNMENT,EX_PARAM,EX_VAL,EX_COMPARISON,EX_CALL,EX_OTHER};

bool is_sign(char c) {
    return c == '=' || c == '<' || c == '>' || c == '!';
}

/*
 * Get an approximate understanding of what a given expression is
 */
expr_type_t expr_type(String fnt, int left, int right) {
    while (1) {
        char c  = fnt[left];
        if (c == ';' || c == '{' || c == ')') {
            left++;
            break;
        } else if (c == '(')
            return EX_PARAM;
        else if (c == '=') {
            char b = fnt[left-1];
            if (is_sign(b))
                return EX_COMPARISON;
            return EX_VAL;
        }

        left--;
        if (left == 0)
            break;
    }

    right -= 1;
    //There is no comparison operator, or assigment operator on the left
    while (1) {
        right++;
        if (right == fnt.length()) {
            break;
        }
        char c = fnt[right];
        if (c == ';' || c == '}') {
            right --;
            break;
        } else if (c == ')')
            return EX_PARAM;
        else if (c == '(')
            return EX_CALL;
        else if (c == '=') {
            char b = fnt[right + 1];
            if (is_sign(b))
                return EX_COMPARISON;
            return EX_ASSIGNMENT;
        }
    }
    //There is no comparison operator on the right, no assigment operator, and not a fnt call
    return EX_OTHER;
}

String
CxxFunction::find_assignment(const String symbol, int stop_at) {

    int n = 0;
    int start_at = 0;
    String val;

    //click_chatter("Searching assignment for %s...", symbol.c_str());
again:
    int pos1, pos2,pos3, match_pos[10], match_len[10];
    if (!find_expr(symbol, &pos1, &pos2, match_pos, match_len, false, false, start_at, stop_at))
        goto done;
     pos3 = pos2;
    if (expr_type(_clean_body, pos1, pos2) == EX_ASSIGNMENT) {
        char c;
        do {

            c = _clean_body[pos2];
            pos2++;
        } while (c != '=');

        int pos3 = pos2 + 1;
        do {
            c = _clean_body[pos3];
            pos3++;
        } while (c != ';' && c != '\n');

        val = _body.substring(pos2, pos3 - pos2 - 1 ).trim();
        //click_chatter("Found assignment for %s : '%s'", symbol.c_str(), val.c_str());
    }
    start_at = pos3;
    n++;
    goto again;
done:
    if (n > 1) {
        click_chatter("Multiple assignment found, ignoring");
    } else if (n == 1){
       return val;
    }
    return "";
}

bool
CxxFunction::replace_expr(const String &pattern, const String &replacement, bool full_symbol, bool all, int start_at)
{
  int pos1, pos2, match_pos[10], match_len[10];
  bool done = false;
again:

  if (!find_expr(pattern, &pos1, &pos2, match_pos, match_len, false, full_symbol, start_at))
    return done;
  //XXX horrible bugfix
  if (pos2 -pos1 == 1 && pattern.length() == 1)
     if (_clean_body[pos1] != pattern[0])
         return done;
  done = true;

  start_at = pos2 + 1;
  if (expr_type(_clean_body, pos1, pos2) == EX_ASSIGNMENT) {
//     click_chatter("Replacement of %s avoided at %d because it's an assignment", pattern.c_str(), start_at);
//     click_chatter("%s", _clean_body.c_str());
//     click_chatter("-->%s", _clean_body.substring(start_at).c_str());
  } else {
      StringAccum sa, clean_sa;
      const char *s = replacement.data();
      const char *end_s = s + replacement.length();
      while (s < end_s) {
        if (*s == '#') {
          assert(s < end_s - 1 && isdigit((unsigned char) s[1]));
          int which = s[1] - '0';
          sa << _body.substring(match_pos[which], match_len[which]);
          clean_sa << _clean_body.substring(match_pos[which], match_len[which]);
          s += 2;
        } else {
          sa << *s;
          clean_sa << *s;
          s++;
        }
      }

      String new_body =
        _body.substring(0, pos1) + sa.take_string() + _body.substring(pos2);
      String new_clean_body =
        _clean_body.substring(0, pos1) + clean_sa.take_string()
        + _clean_body.substring(pos2);
      _body = new_body;
      _clean_body = new_clean_body;
  }

  if (all)
      goto again;
  //fprintf(stderr, ">>>>>> %s\n", _body.c_str());
  return true;
}

int
CxxFunction::replace_call(const String &pattern, const String &replacement, Vector<String> &args) {

      int pos1, pos2, match_pos[10], match_len[10] = {-1};
      if (!find_expr(pattern, &pos1, &pos2, match_pos, match_len, true, true))
        return -1;

      int i = 0;
      while (match_len[i] > -1) {
          args.push_back(_body.substring(match_pos[i], match_len[i]));
          i++;
      }

      //click_chatter("Found %s pos %d pos %d match %d %d : %s",pattern.c_str(), pos1, pos2, match_pos[0], match_len[0], _body.substring(pos1, pos2 - pos1).c_str());
      //fprintf(stderr, ":::::: %s\n", _body.c_str());

      StringAccum sa, clean_sa;
      const char *s = replacement.data();
      const char *end_s = s + replacement.length();
      while (s < end_s) {
        if (*s == '#') {
          assert(s < end_s - 1 && isdigit((unsigned char) s[1]));
          int which = s[1] - '0';
          sa << _body.substring(match_pos[which], match_len[which]);
          clean_sa << _clean_body.substring(match_pos[which], match_len[which]);
          s += 2;
        } else {
          sa << *s;
          clean_sa << *s;
          s++;
        }
      }

      String new_body =
        _body.substring(0, pos1) + sa.take_string() + _body.substring(pos2);
      String new_clean_body =
        _clean_body.substring(0, pos1) + clean_sa.take_string()
        + _clean_body.substring(pos2);
      _body = new_body;
      _clean_body = new_clean_body;

      //fprintf(stderr, ">>>>>> %s\n", _body.c_str());
      return pos2;
}


/*****
 * CxxClass
 **/

CxxClass::CxxClass(const String &name)
  : _name(name), _fn_map(-1)
{
  //fprintf(stderr, "!!!!!%s\n", _name.c_str());
}

void
CxxClass::add_parent(CxxClass *cxx,const String str)
{
    ParentalLink l;
    l.parent = cxx;
    l.template_params = str;
  _parents.push_back(l);
}

CxxFunction &
CxxClass::defun(const CxxFunction &fn, const bool &rewrite)
{
    for (int i = 0; i < _functions.size(); i++) {
        if (_functions[i].name() == fn.name()) {
            _functions[i] = fn;
              if (rewrite)
               _should_rewrite[i] = true;
            return _functions[i];
        }
    }
    int which = _functions.size();
  _functions.push_back(fn);
  _fn_map.set(fn.name(), which);
  _functions.back().unkill();
  if (_should_rewrite.size() < which+1)
    _should_rewrite.resize(which + 1);
  if (rewrite)
      _should_rewrite[which] = true;
  return _functions.back();
}

bool
CxxClass::reach(int findex, Vector<int> &reached)
{
  if (findex < 0)
    return false;
  if (reached[findex])
    return _should_rewrite[findex];
  reached[findex] = true;

  // return true if reachable and rewritable
  const String &clean_body = _functions[findex].clean_body();
  const char *s = clean_body.data();
  int p = 0;
  int len = clean_body.length();
  bool should_rewrite = _has_push[findex] || _has_pull[findex];

  while (p < len) {

    // look for a function call
    while (p < len && s[p] != '(')
      p++;
    if (p >= len)
      break;

    int paren_p = p;
    for (p--; p >= 0 && isspace((unsigned char) s[p]); p--)
      /* nada */;
    if (p < 0 || (!isalnum((unsigned char) s[p]) && s[p] != '_')) {
      p = paren_p + 1;
      continue;
    }
    int end_word_p = p + 1;
    while (p >= 0 && (isalnum((unsigned char) s[p]) || s[p] == '_'))
      p--;
    int start_word_p = p + 1;
    while (p >= 0 && isspace((unsigned char) s[p]))
      p--;

    // have found word; check that it is a direct call
    if (p >= 0 && (s[p] == '.' || (p > 0 && s[p-1] == '-' && s[p] == '>')))
      /* do nothing; a call of some object */;
    else {
      // XXX class-qualified?
      String name = clean_body.substring(start_word_p, end_word_p - start_word_p);
      int findex2 = _fn_map.get(name);
      if (findex2 >= 0 && reach(findex2, reached))
    should_rewrite = true;
    }

    // skip past word
    p = paren_p + 1;
  }

  if (!should_rewrite && !_functions[findex].from_header_file()) {
    // might still be rewritable if it's inlined from the
    // .cc file, which we can't #include
    const String &ret_type = _functions[findex].ret_type();
    const char *s = ret_type.data();
    int len = ret_type.length();
    for (int p = 0; p < len - 6; p++)
      if (s[p+0] == 'i' && s[p+1] == 'n' && s[p+2] == 'l'
      && s[p+3] == 'i' && s[p+4] == 'n' && s[p+5] == 'e'
      && (p == 0 || isspace((unsigned char) s[p-1]))
      && (p == len-6 || isspace((unsigned char) s[p+6]))) {
    should_rewrite = true;
    break;
      }
  }

  _should_rewrite[findex] = should_rewrite;
  return should_rewrite;
}

CxxClass::RewriteStatus
CxxClass::find_should_rewrite()
{
  _has_push.assign(nfunctions(), 0);
  _has_pull.assign(nfunctions(), 0);
  _should_rewrite.assign(nfunctions(), 0);

  if (_fn_map.get("never_devirtualize") >= 0)
    return REWRITE_NEVER;

  static String push_pattern = compile_pattern("output(#0).push(#1)");
  static String o_push_pattern = compile_pattern("output_push(#0,#1)");
  static String pull_pattern = compile_pattern("input(#0).pull()");
  static String checked_push_pattern = compile_pattern("checked_output_push(#0,#1)");

#if HAVE_BATCH
  static String push_batch_pattern = compile_pattern("output(#0).push_batch(#1)");
  static String o_push_batch_pattern = compile_pattern("output_push_batch(#0,#1)");
  static String checked_push_batch_pattern = compile_pattern("checked_output_push_batch(#0,#1)");
#endif
  for (int i = 0; i < nfunctions(); i++) {
    if (_functions[i].find_expr(push_pattern)
    || _functions[i].find_expr(o_push_pattern)
#if HAVE_BATCH
    || _functions[i].find_expr(push_batch_pattern)
    || _functions[i].find_expr(o_push_batch_pattern)
    || _functions[i].find_expr(checked_push_batch_pattern)
#endif
    || _functions[i].find_expr(checked_push_pattern))
      _has_push[i] = 1;
    if (_functions[i].find_expr(pull_pattern))
      _has_pull[i] = 1;
  }

  Vector<int> reached(nfunctions(), 0);
  bool any = reach(_fn_map.get("push"), reached);
#if HAVE_BATCH
  any |= reach(_fn_map.get("push_batch"), reached);
#endif
  any |= reach(_fn_map.get("pull"), reached);
  any |= reach(_fn_map.get("run_task"), reached);
  any |= reach(_fn_map.get("run_timer"), reached);
  any |= reach(_fn_map.get("selected"), reached);
  int simple_action = _fn_map.get("simple_action");
  if (simple_action >= 0) {
    reach(simple_action, reached);
    _should_rewrite[simple_action] = any = true;
  }
#if HAVE_BATCH
  int simple_action_batch = _fn_map.get("simple_action_batch");
  if (simple_action_batch >= 0) {
    reach(simple_action_batch, reached);
    _should_rewrite[simple_action_batch] = any = true;
  }
#endif
  if (_fn_map.get("devirtualize_all") >= 0) {
    for (int i = 0; i < nfunctions(); i++) {
      const String &n = _functions[i].name();
      if (n != _name && n[0] != '~')
    _should_rewrite[i] = any = true;
    }
  }

  if (!any)
      click_chatter("Shouldn't rewrite %s", name().c_str());
  return any?REWRITE_YES:REWRITE_NO;
}

void
CxxClass::header_text(StringAccum &sa, int align=0) const
{
  sa << "class ";
  if (align)
    sa << "alignas(" << align <<") ";
  sa << _name;
  if (_parents.size()) {
    sa << " : ";
    for (int i = 0; i < _parents.size(); i++) {
      if (i) sa << ", ";
      sa << "public " << _parents[i].parent->name();
    }
  }
  sa << " {\n public:\n";
  for (int i = 0; i < _functions.size(); i++) {
    const CxxFunction &fn = _functions[i];
    if (fn.alive()) {
      sa << "  " << fn.ret_type() << " " << fn.name() << fn.args();
      if (fn.in_header())
    sa << " {" << fn.body() << "}\n";
      else
    sa << ";\n";
    }
  }
  sa << "};\n";
}

void
CxxClass::source_text(StringAccum &sa) const
{
  for (int i = 0; i < _functions.size(); i++) {
    const CxxFunction &fn = _functions[i];
    if (fn.alive() && !fn.in_header()) {
      sa << fn.ret_type() << "\n" << _name << "::" << fn.name() << fn.args();
      sa << "\n{" << fn.body() << "}\n";
    }
  }
}


/*****
 * CxxInfo
 **/

CxxInfo::CxxInfo()
  : _class_map(-1)
{
}

CxxInfo::~CxxInfo()
{
  for (int i = 0; i < _classes.size(); i++)
    delete _classes[i];
}

CxxClass *
CxxInfo::make_class(const String &name)
{
  int &which = _class_map[name];
  if (which < 0) {
    which = _classes.size();
    _classes.push_back(new CxxClass(name));
  }
  return _classes[which];
}

static String
remove_crap(const String &original_text)
{
  // Get rid of preprocessor directives, comments, string literals, and
  // character literals by replacing them with the right number of spaces.

  const char *s = original_text.data();
  const char *end_s = s + original_text.length();

  StringAccum new_text;
  char *o = new_text.extend(original_text.length());

  char *if0_o_ptr = 0;

  while (s < end_s) {
    // read one line

    // skip spaces at beginning of line
    while (s < end_s && isspace((unsigned char) *s))
      *o++ = *s++;

    if (s >= end_s)        // end of data
      break;

    if (*s == '#') {        // preprocessor directive
      const char *first_s = s;
      while (1) {
    while (s < end_s && *s != '\n' && *s != '\r')
      *o++ = ' ', s++;
    bool backslash = (s[-1] == '\\');
    while (s < end_s && (*s == '\n' || *s == '\r'))
      *o++ = *s++;
    if (!backslash)
      break;
      }
      // check for '#if 0 .. #endif'
      const char *ss = first_s + 1;
      while (ss < s && isspace((unsigned char) *ss))
    ss++;
      if (ss < s - 5 && ss[0] == 'e' && ss[1] == 'n' && ss[2] == 'd'
      && ss[3] == 'i' && ss[4] == 'f') {
    if (if0_o_ptr)
      while (if0_o_ptr < o)
        *if0_o_ptr++ = ' ';
    if0_o_ptr = 0;
      } else if (ss < s - 3 && ss[0] == 'i' && ss[1] == 'f') {
    for (ss += 2; ss < s && isspace((unsigned char) *ss); ss++) ;
    if (ss < s && ss[0] == '0')
      if0_o_ptr = o;
      }
      continue;
    }

    // scan; stop at EOL, comment start, or literal start
    while (s < end_s && *s != '\n' && *s != '\r') {

      // copy chars
      while (s < end_s && *s != '/' && *s != '\"' && *s != '\''
         && *s != '\n' && *s != '\r')
    *o++ = *s++;

      if (s < end_s - 1 && *s == '/' && s[1] == '*') {
    // slash-star comment
    *o++ = ' ';
    *o++ = ' ';
    s += 2;
    while (s < end_s && (*s != '*' || s >= end_s - 1 || s[1] != '/')) {
      *o++ = (*s == '\n' || *s == '\r' ? *s : ' ');
      s++;
    }
    if (s < end_s) {
      *o++ = ' ';
      *o++ = ' ';
      s += 2;
    }

      } else if (s < end_s - 1 && *s == '/' && s[1] == '/') {
    // slash-slash comment
    *o++ = ' ';
    *o++ = ' ';
    s += 2;
    while (s < end_s && *s != '\n' && *s != '\r')
      *o++ = ' ', s++;

      } else if (*s == '\"' || *s == '\'') {
    // literal
    // XXX I am not sure why the closing quote,
    // and any characters preceded by backslash, are turned into $.
    char stopper = *s;
    *o++ = ' ', s++;
    while (s < end_s && *s != stopper) {
      *o++ = ' ', s++;
      if (s[-1] == '\\')
        *o++ = '$', s++;
    }
    if (s < end_s)
      *o++ = '$', s++;

      } else if (*s != '\n' && *s != '\r')
    // random other character, fine
    *o++ = *s++;
    }

    // copy EOL characters
    while (s < end_s && (*s == '\n' || *s == '\r'))
      *o++ = *s++;
  }

  return new_text.take_string();
}

static int
skip_balanced_braces(const String &text, int p)
{
  const char *s = text.data();
  int len = text.length();
  int brace_level = 0;
  while (p < len) {
    if (s[p] == '{')
      brace_level++;
    else if (s[p] == '}') {
      if (!--brace_level)
    return p + 1;
    }
    p++;
  }
  return p;
}

static int
skip_balanced_parens(const String &text, int p)
{
  const char *s = text.data();
  int len = text.length();
  int brace_level = 0;
  while (p < len) {
    if (s[p] == '(')
      brace_level++;
    else if (s[p] == ')') {
      if (!--brace_level)
    return p + 1;
    }
    p++;
  }
  return p;
}

/*
 * Parse functions but ignores declarations
 */
int
CxxInfo::parse_function_definition(const String &text, int fn_start_p,
                   int paren_p, const String &original,
                   CxxClass *cxx_class)
{
  // find where we think open brace should be
  int p = skip_balanced_parens(text, paren_p);
  const char *s = text.data();
  int len = text.length();
  while (p < len && isspace((unsigned char) s[p]))
    p++;
  if (p < len - 5 && strncmp(s+p, "const", 5) == 0) {
    for (p += 5; p < len && isspace((unsigned char) s[p]); p++)
      /* nada */;
  }
  // if open brace is not there, a function declaration or something similar;
  // return
  if ( p < len + 5 && strncmp(s+p, "final", 5) == 0) {
      p+= 5;
     while (p < len && isspace((unsigned char) s[p]))
        p++;
  }
  if (p >= len || s[p] != '{') {
      //click_chatter("No body :(");
      return p;
  }

  // save boundaries of function body
  int open_brace_p = p;
  int close_brace_p = skip_balanced_braces(text, open_brace_p);

  // find arguments; cut space from end
  for (p = open_brace_p - 1; p >= paren_p && isspace((unsigned char) s[p]); p--)
    /* nada */;
  String args = original.substring(paren_p, p + 1 - paren_p);

  // find function name and class name
  for (p = paren_p - 1; p > fn_start_p && isspace((unsigned char) s[p]); p--)
    /* nada */;
  int end_fn_name_p = p + 1;
  while (p >= fn_start_p && (isalnum((unsigned char) s[p]) || s[p] == '_' || s[p] == '~'))
    p--;
  String fn_name = original.substring(p + 1, end_fn_name_p - (p + 1));
  String class_name;
  if (p >= fn_start_p + 2 && s[p] == ':' && s[p-1] == ':') {
    int end_class_name_p = p - 1;
    for (p -= 2; p >= fn_start_p && (isalnum((unsigned char) s[p]) || s[p] == '_' || s[p] == '~'); p--)
      /* nada */;
    if (p > fn_start_p && s[p] == ':') // nested class fns uninteresting
      return close_brace_p;
    class_name = original.substring(p + 1, end_class_name_p - (p + 1));
    //click_chatter("Class name %s", class_name.c_str());
  }

  // find return type; skip access control declarations, cut space from end
  while (1) {
    int access_p;
    if (p >= fn_start_p + 6 && memcmp(s+fn_start_p, "public", 6) == 0)
      access_p = fn_start_p + 6;
    else if (p >= fn_start_p + 7 && memcmp(s+fn_start_p, "private", 7) == 0)
      access_p = fn_start_p + 7;
    else if (p >= fn_start_p + 9 && memcmp(s+fn_start_p, "protected", 9) == 0)
      access_p = fn_start_p + 9;
    else if (p >= fn_start_p + 11 && memcmp(s+fn_start_p, "CLICK_DECLS", 11) == 0)
      access_p = fn_start_p + 11;
    else if (p >= fn_start_p + 14 && memcmp(s+fn_start_p, "CLICK_ENDDECLS", 14) == 0)
      access_p = fn_start_p + 14;
    else
      break;
    while (access_p < p && isspace((unsigned char) s[access_p]))
      access_p++;
    if (access_p < p && s[access_p] == ':')
      access_p++;
    for (; access_p < p && isspace((unsigned char) s[access_p]); access_p++)
      /* nada */;
    fn_start_p = access_p;
  }
  while (p >= fn_start_p && isspace((unsigned char) s[p]))
    p--;
  String ret_type = original.substring(fn_start_p, p + 1 - fn_start_p);

  // decide if this function/class pair is OK
  CxxClass *relevant_class;
  if (class_name)
    relevant_class = make_class(class_name);
  else
    relevant_class = cxx_class;

  // define function
  if (relevant_class) {
    int body_pos = open_brace_p + 1;
    int body_len = close_brace_p - 1 - body_pos;
    relevant_class->defun
      (CxxFunction(fn_name, !class_name, ret_type, args,
           original.substring(body_pos, body_len),
           text.substring(body_pos, body_len)));
  } else {
      //click_chatter("Not relevant:(");
  }
  // done
  return close_brace_p;
}

int parse_reentrant(String s, int p, char b, char e, int len) {
    int n = 0;
    while(1) {
        p++;
        if (p == len) {
            return p;
        }
        if (s[p] == b)
            n++;
        if (s[p] == e) {
            n--;
            if (n == 0) return p +1;
        }
    }

}
int
CxxInfo::parse_class_definition(const String &text, int p,
                const String &original, CxxClass * &cxxc)
{
  // find class name
  const char *s = text.data();
  int len = text.length();
  while (p < len && isspace((unsigned char) s[p]))
    p++;
  int name_start_p = p;
  while (p < len && (isalnum((unsigned char) s[p]) || s[p] == '_'))
    p++;
  String class_name = original.substring(name_start_p, p - name_start_p);
  cxxc = make_class(class_name);

  // parse superclasses
  while (p < len && s[p] != '{') {
    while (p < len && s[p] != '{' && !isalnum((unsigned char) s[p]) && s[p] != '_')
      p++;
    int p1 = p;
    while (p < len && (isalnum((unsigned char) s[p]) || s[p] == '_'))
      p++;
    if (p > p1 && (p != p1 + 6 || strncmp(s+p1, "public", 6) != 0)) {
      // XXX private or protected inheritance?
     //click_chatter("Parent %s", original.substring(p1, p - p1).c_str());
      CxxClass *parent = make_class(original.substring(p1, p - p1));
    //Parse template parameters
      String params = "";
      if (s[p] == '<') {
          int e = parse_reentrant(s, p -1 , '<','>', len);
          if (e >= len)
              click_chatter("Malformed template parameters!?");

          params = original.substring(p + 1, e - p - 2 );
          click_chatter("Template params '%s'", params.c_str());
          p = e + 1;
      }

      cxxc->add_parent(parent, params);
    }
  }

  // parse class body
  int c = parse_class(text, p + 1, original, cxxc);
  //cxxc->print_function_list();
  return c;
}

int
CxxInfo::parse_class(const String &text, int p, const String &original,
             CxxClass *cxx_class)
{
  // parse clean_text
  const char *s = text.data();
  int len = text.length();
//click_chatter("Parsing class at %d [%c]",p,text[p]);
  while (1) {

    // find first batch
    while (p < len && isspace((unsigned char) s[p]))
      p++;
    int p1 = p;
    while (p < len && s[p] != ';' && s[p] != '(' && s[p] != '{' &&
       s[p] != '}')
      p++;

    //fprintf(stderr, "   %d %c\n", p, s[p]);
    if (p >= len) {
      return len;
    }
    else if (s[p] == ';') {
      // uninteresting
      p++;
      continue;
    } else if (s[p] == '}') {
      //fprintf(stderr, " end of class at %d !!!!!!/\n",p+1);
      return p + 1;
    } else if (s[p] == '{') {

        CxxClass* child_cxx = 0;
      if (p > p1 + 6 && !cxx_class
      && (strncmp(s+p1, "class", 5) == 0
          || strncmp(s+p1, "struct", 6) == 0)) {
        // parse class definition
            //click_chatter("Parsing definition at %d of %s", p1 + 6, text.substring(p1+6, 20).c_str());
        p = parse_class_definition(text, p1 + 6, original, child_cxx);
            //      click_chatter("Subclass %s", text.substring(p1+6,p-p1-6).c_str() );
      } else if (p > p1 + 8 && !cxx_class
      && (strncmp(s+p1, "template", 8) == 0)) {

        //click_chatter("Parsing template definition at %d of %s", p1, text.substring(p1, text.substring(p1).find_left('\n')).c_str());
        int p2 = p1+8;

        while (p2 < len && isspace((unsigned char) s[p2])) p2++;
        p1 = p2;
        p2 = parse_reentrant(s, p2 - 1 , '<','>', len);
        String tmpl =  text.substring(p1 + 1, p2 - p1 -2);
        p1 = p2;
        while (p1 < len && isspace((unsigned char) s[p1])) p1++;
        p = parse_class_definition(s, p1 + 6, original, child_cxx);
        if (child_cxx) {
            //click_chatter("Class %s had tmpl param %s", child_cxx->name().c_str(), tmpl.c_str());
            child_cxx->set_template(tmpl);;
        }

            //      click_chatter("Subclass %s", text.substring(p1+6,p-p1-6).c_str() );
      } else

    p = skip_balanced_braces(text, p);
    } else if (s[p] == '(') {
        //click_chatter("Parse fun %s",text.substring(p1,p-p1).c_str());
      p = parse_function_definition(text, p1, p, original, cxx_class);
    }

  }
}

void
CxxInfo::parse_file(const String &original_text, bool header,
            String *store_includes)
{
  String clean_text = remove_crap(original_text);
  CxxFunction::parsing_header_file = header;
  parse_class(clean_text, 0, original_text, 0);

  // save initial comments and #defines and #includes for replication.
  // Also skip over 'CLICK_CXX_whatever', enum definitions, typedefs,
  // and 'extern "C" { }' blocks enclosing headers only.
  // XXX Should save up to an arbitrary comment or something
  if (store_includes) {
    const char *s = clean_text.data();
    int p = 0;
    int len = clean_text.length();
    while (1) {
      while (p < len && isspace((unsigned char) s[p]))
    p++;

      if (p < len && s[p] == ';') {
    // mop up stray semicolons
    p++;

      } else if (p + 7 < len && memcmp(s + p, "extern", 6) == 0
         && isspace((unsigned char) s[p+6])) {
    // include 'extern ["C"] { -HEADERS- }'
    int p1 = p + 6;
    while (p1 < len && (isspace((unsigned char) s[p1]) || s[p1] == '$'))
      p1++;
    if (p1 >= len || s[p1] != '{')
      break;
    for (p1++; p1 < len && isspace((unsigned char) s[p1]); p1++)
      /* nada */;
    if (p1 >= len || s[p1] != '}')
      break;
    p = p1 + 1;

      } else if (p + 5 < len && memcmp(s + p, "enum", 4) == 0
         && isspace((unsigned char) s[p+4])) {
    // include 'enum [IDENTIFIER] { ... }'
    int p1 = p + 5;
    while (p1 < len && isspace((unsigned char) s[p1]))
      p1++;
    if (p1 < len && (isalnum((unsigned char) s[p1]) || s[p1] == '_')) {
      while (p1 < len && (isalnum((unsigned char) s[p1]) || s[p1] == '_'))
        p1++;
      while (p1 < len && isspace((unsigned char) s[p1]))
        p1++;
    }
    if (p1 >= len || s[p1] != '{')
      break;
    for (p1++; p1 < len && s[p1] != '}'; p1++)
      /* nada */;
    if (p1 >= len)
      break;
    p = p1 + 1;

      } else if (p + 8 < len && memcmp(s + p, "typedef", 7) == 0
         && isspace((unsigned char) s[p+7])) {
    // include typedefs
    for (p += 8; p < len && s[p] != ';'; p++)
      /* nada */;

      } else if (p + 9 < len && memcmp(s + p, "CLICK_CXX", 9) == 0) {
    // include 'CLICK_CXX' (used in <click/cxxprotect.h>)
    for (p += 9; p < len && (isalnum((unsigned char) s[p]) || s[p] == '_'); p++)
      /* nada */;

      } else
    break;
    }
    *store_includes = original_text.substring(0, p);
  }
}

void CxxClass::print_function_list() {
      click_chatter("Function list:");
      auto begin = _fn_map.begin();
      auto end = _fn_map.end();
      while (begin != end) {
          click_chatter("FN[%s] %s", (*begin).first.c_str(), _functions[(*begin).second].name().c_str());
          begin++;
      }
      for (int i = 0; i < _functions.size(); i++) {
          click_chatter("FN[%d] %s", i, _functions[i].name().c_str());
      }
}

CxxFunction*
CxxClass::find_in_parent(const String& name, const String& outer_class) {
    for (int i = 0; i < nparents(); i++) {
        /*
        click_chatter("Searching %s in %s", name.c_str(), parent(i)->name().c_str());
        parent(i)->print_function_list();
        */
        CxxFunction* f = parent(i)->find(name);
        if (!f)
            f = parent(i)->find_in_parent(name, outer_class);
        if (f) {
            if (parent(i)->_template) {
                String s = parent_tmpl(i);
                if (!s) {
                    click_chatter("Template parent without template parameters...");
                    return 0;
                }

                Vector<String> args = parent(i)->_template.split(',');
                Vector<String> vals = s.split(',');

                CxxFunction* c = new CxxFunction(*f);
                for (int i = 0; i < args.size(); i++) {
                    String arg = args[i].trim();
                    arg = arg.substring(arg.find_left(' ') + 1);
                    String val = vals[i].trim();
                    if (val == this->name()) {
                        //click_chatter("CRTP detected, replacing with uttermost");
                        val = outer_class;
                    }
                    //click_chatter("Replacing %s with %s",arg.c_str(),val.c_str());
                    c->replace_expr(arg,val,true,true);
                }
                return c;
            }

            return f;
        }
    }
    return 0;
}
