/*
 * specializer.{cc,hh} -- specializer
 * Eddie Kohler
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
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
#include <click/pathvars.h>

#include "specializer.hh"
#include "routert.hh"
#include <click/error.hh>
#include "toolutils.hh"
#include "elementmap.hh"
#include <click/straccum.hh>
#include "signature.hh"
#include <ctype.h>

Vector<Pair<String,String>> patterns;

Specializer::Specializer(RouterT *router, const ElementMap &em)
  : _router(router), _nelements(router->nelements()),
    _ninputs(router->nelements(), 0), _noutputs(router->nelements(), 0),
    _etinfo_map(0), _header_file_map(-1), _parsed_sources(-1), _do_inline(false),
    _do_static(false), _do_unroll(false), _unroll_val(0), _do_switch(false), _verbose(false), _switch_burst(0),
    _do_jmps(false), _jmp_burst(0), _do_align(0)
{
  _etinfo.push_back(ElementTypeInfo());

  for (RouterT::iterator x = router->begin_elements(); x; x++) {
    _noutputs[x->eindex()] = x->noutputs();
    _ninputs[x->eindex()] = x->ninputs();
  }

  patterns.push_back(Pair<String,String>("read(#0,#1)","validate(#0!TEMPVAL!)"));
  //patterns.push_back(Pair<String,String>("read_p(#0,#1)","validate(#0!TEMPVAL!)"));
  //patterns.push_back(Pair<String,String>("read_mp(#0,#1)","validate(#0!TEMPVAL!)"));
  patterns.push_back(Pair<String,String>("read_or_set(#0,#1,#2)","validate(#0!TEMPVAL!)"));
  patterns.push_back(Pair<String,String>("read_or_set_p(#0,#1,#2)","validate_p(#0!TEMPVAL!)"));

  // prepare from element map
  for (ElementMap::TraitsIterator x = em.begin_elements(); x; x++) {
    const Traits &e = x.value();
    //click_chatter("%s %s", e.header_file.c_str(), em.source_directory(e).c_str());
    add_type_info(e.name, e.cxx, e.header_file, em.source_directory(e));
  }


  add_type_info("FlowSpaceElement", "FlowSpaceElement", "flowelement.hh", "/usr/local/include/click/flow/");
  add_type_info("FlowStateElement", "FlowStateElement", "flowelement.hh", "/usr/local/include/click/flow/");

}

inline ElementTypeInfo &
Specializer::etype_info(int eindex)
{
  return type_info(_router->etype_name(eindex));
}

inline const ElementTypeInfo &
Specializer::etype_info(int eindex) const
{
  return type_info(_router->etype_name(eindex));
}

void
Specializer::add_type_info(const String &click_name, const String &cxx_name,
               const String &header_file, const String &source_dir)
{
  ElementTypeInfo eti;
  eti.click_name = click_name;
  eti.cxx_name = cxx_name;
  eti.header_file = header_file;
  eti.source_directory = source_dir;
  _etinfo.push_back(eti);

  int i = _etinfo.size() - 1;
  _etinfo_map.set(click_name, i);

  if (header_file) {
    int slash = header_file.find_right('/');
    _header_file_map.set(header_file.substring(slash < 0 ? 0 : slash + 1),
             i);
  }
}

void
ElementTypeInfo::locate_header_file(RouterT *for_archive, ErrorHandler *errh)
{
  if (!found_header_file) {
    if (!source_directory && for_archive->archive_index(header_file) >= 0)
      found_header_file = header_file;
    else if (String found = clickpath_find_file(header_file, 0, source_directory))
      found_header_file = found;
    else {
      errh->warning("can%,t locate header file \"%s\"", header_file.c_str());
      found_header_file = header_file;
    }
  }
}

void
Specializer::parse_source_file(ElementTypeInfo &etinfo,
                   bool do_header, String *includes)
{
  String fn = etinfo.header_file;
  if (!do_header && fn.substring(-3) == ".hh")
    fn = etinfo.header_file.substring(0, -3) + ".cc";

  // don't parse a source file twice
  if (_parsed_sources.get(fn) < 0) {
    String text;
    if (!etinfo.source_directory && _router->archive_index(fn) >= 0) {
      text = _router->archive(fn).data;
      if (do_header)
    etinfo.found_header_file = fn;
    } else if (String found = clickpath_find_file(fn, 0, etinfo.source_directory)) {
      text = file_string(found);
      if (do_header)
    etinfo.found_header_file = found;
    }

    _cxxinfo.parse_file(text, do_header, includes);
    _parsed_sources.set(fn, 1);
  }
}

void
Specializer::read_source(ElementTypeInfo &etinfo, ErrorHandler *errh)
{
  if (!etinfo.click_name || etinfo.read_source) {
      click_chatter("Not reading source - %d %d",!etinfo.click_name, etinfo.read_source);
    return;
  }

  etinfo.read_source = true;
  if (!etinfo.header_file) {
    errh->warning("element class %<%s%> has no source file", etinfo.click_name.c_str());
    return;
  }

  // parse source text
  String text, filename = etinfo.header_file;
  if (filename.substring(-3) == ".hh")
    parse_source_file(etinfo, true, 0);
  parse_source_file(etinfo, false, &etinfo.includes);

  // now, read source for the element class's parents
  CxxClass *cxxc = _cxxinfo.find_class(etinfo.cxx_name);
  //cxxc->print_function_list();
  if (cxxc)
    for (int i = 0; i < cxxc->nparents(); i++) {
      const String &p = cxxc->parent(i)->name();
      //click_chatter("%s - %s", type_info(p).click_name.c_str(), p.c_str());
      if (p != "Element")
        read_source(type_info(p), errh);
    }
}

void
Specializer::check_specialize(int eindex, ErrorHandler *errh)
{
  int sp = _specialize[eindex];
    //click_chatter("SP state %d",_specials[sp].eindex);
  if (_specials[sp].eindex > SPCE_NOT_DONE)
    return;
  _specials[sp].eindex = SPCE_NOT_SPECIAL;

  // get type info
  ElementTypeInfo &old_eti = etype_info(eindex);
  if (!old_eti.click_name) {
    errh->warning("no information about element class %<%s%>",
          _router->etype_name(eindex).c_str());
    return;
  }

  // read source code
  if (!old_eti.read_source) {
    read_source(old_eti, errh);
  }
  CxxClass *old_cxxc = _cxxinfo.find_class(old_eti.cxx_name);
  if (!old_cxxc) {
    errh->warning("C++ class %<%s%> not found for element class %<%s%>",
          old_eti.cxx_name.c_str(), old_eti.click_name.c_str());
    return;
  }

  // don't specialize if there are no reachable functions
  SpecializedClass &spc = _specials[sp];
  spc.old_click_name = old_eti.click_name;
  spc.eindex = eindex;
  CxxClass::RewriteStatus rw = old_cxxc->find_should_rewrite();
  click_chatter("%s : %d", old_eti.click_name.c_str(), rw);
  if (rw != CxxClass::REWRITE_NEVER && !_do_replace && !_do_static) {
    spc.click_name = spc.old_click_name;
    spc.cxx_name = old_eti.cxx_name;
  } else {
    spc.click_name = specialized_click_name(_router->element(eindex));
    spc.cxx_name = click_to_cxx_name(spc.click_name);
    add_type_info(spc.click_name, spc.cxx_name, String(), String());
  }
}

bool
Specializer::create_class(SpecializedClass &spc)
{
  assert(!spc.cxxc);
  int eindex = spc.eindex;
  if (spc.click_name == spc.old_click_name)
    return false;

  //click_chatter("Specializing %s",spc.click_name.c_str());
  // create new C++ class
  const ElementTypeInfo &old_eti = etype_info(eindex);
  CxxClass *old_cxxc = _cxxinfo.find_class(old_eti.cxx_name);
  CxxClass *new_cxxc = _cxxinfo.make_class(spc.cxx_name);
  assert(old_cxxc && new_cxxc);
  bool specialize_away = (old_cxxc->find("devirtualize_all") != 0);
  String parent_cxx_name = old_eti.cxx_name;
  if (specialize_away) {
    CxxClass *parent = old_cxxc->parent(0);
    new_cxxc->add_parent(parent);
    parent_cxx_name = parent->name();
  } else
    new_cxxc->add_parent(old_cxxc);
  spc.cxxc = new_cxxc;

  // add helper functions: constructor, destructor, class_name, cast
  if (specialize_away) {
    CxxFunction *f = old_cxxc->find(old_eti.cxx_name);
    new_cxxc->defun
      (CxxFunction(spc.cxx_name, true, "", "()", f->body(), f->clean_body()));

    f = old_cxxc->find("~" + old_eti.cxx_name);
    new_cxxc->defun
      (CxxFunction("~" + spc.cxx_name, true, "", "()", f->body(), f->clean_body()));
  } else {
    new_cxxc->defun
      (CxxFunction(spc.cxx_name, true, "", "()", " ", ""));
    new_cxxc->defun
      (CxxFunction("~" + spc.cxx_name, true, "", "()", " ", ""));
  }

  new_cxxc->defun
    (CxxFunction("class_name", true, "const char *", "() const",
         String(" return \"") + spc.click_name + "\"; ", ""));
  new_cxxc->defun
    (CxxFunction("cast", false, "void *", "(const char *n)",
         "\n  if (void *v = " + parent_cxx_name + "::cast(n))\n\
    return v;\n  else if (strcmp(n, \"" + spc.click_name + "\") == 0\n\
      || strcmp(n, \"" + old_eti.click_name + "\") == 0)\n\
    return (Element *)this;\n  else\n    return 0;\n", ""));

  // placeholders for input_pull and output_push
  new_cxxc->defun
    (CxxFunction("input_pull", false, "inline Packet *",
         (_ninputs[eindex] ? "(int i) const" : "(int) const"),
         "", ""));
  new_cxxc->defun
    (CxxFunction("output_push", false, "inline void",
         (_noutputs[eindex] ? "(int i, Packet *p) const" : "(int, Packet *p) const"),
         "", ""));
#if HAVE_BATCH
  new_cxxc->defun
    (CxxFunction("output_push_batch", false, "inline void",
         (_noutputs[eindex] ? "(int i, PacketBatch *p) const" : "(int, PacketBatch *p) const"),
         "", ""));
#endif
  new_cxxc->defun
    (CxxFunction("output_push_checked", false, "inline void",
         (_noutputs[eindex] ? "(int i, Packet *p) const" : "(int, Packet *p) const"),
         "", ""));
#if HAVE_BATCH
  new_cxxc->defun
    (CxxFunction("output_push_batch_checked", false, "inline void",
         (_noutputs[eindex] ? "(int i, PacketBatch *p) const" : "(int, PacketBatch *p) const"),
         "", ""));
#endif
  new_cxxc->defun
    (CxxFunction("never_devirtualize", true, "void", "()", "", ""));


      //IF IMPROVED
//      CxxClass* new_cxxc = _specials[s].cxxc;
      if (!new_cxxc->find("push_batch")) {
        CxxFunction* f = new_cxxc->find_in_parent("push_batch", new_cxxc->name());

        if (f)
            old_cxxc->defun(*f, true);
      }

  // transfer reachable rewritable functions to new C++ class
  // with pattern replacements
  {
    String ninputs_pat = compile_pattern("ninputs()");
    String ninputs_repl = String(_ninputs[eindex]);
    String noutputs_pat = compile_pattern("noutputs()");
    String noutputs_repl = String(_noutputs[eindex]);
    String push_pat = compile_pattern("output(#0).push(#1)");
    String push_repl = "output_push(#0, #1)";
#if HAVE_BATCH
    String push_batch_pat = compile_pattern("output(#0).push_batch(#1)");
    String push_batch_repl = "output_push_batch(#0, #1)";
#endif
    String checked_push_pat = compile_pattern("checked_output_push(#0, #1)");
    String checked_push_repl = compile_pattern("output_push_checked(#0, #1)");
#if HAVE_BATCH
    String checked_push_batch_pat = compile_pattern("checked_output_push_batch(#0, #1)");
    String checked_push_batch_repl = compile_pattern("output_push_batch_checked(#0, #1)");

    String sym_checked_push_batch_pat = compile_pattern("checked_output_push_batch");
    String sym_checked_push_batch_repl = compile_pattern("output_push_batch_checked");
#endif
    String pull_pat = compile_pattern("input(#0).pull()");
    String pull_repl = "input_pull(#0)";
    bool any_checked_push = false, any_push = true, any_pull = false;

    for (int i = 0; i < old_cxxc->nfunctions(); i++)
      if (old_cxxc->should_rewrite(i)) {
        const CxxFunction &old_fn = old_cxxc->function(i);

    //    click_chatter("Should rewrite %s",old_fn.name().c_str());
        if (new_cxxc->find(old_fn.name())) { // don't add again
          continue;
        }
        CxxFunction &new_fn = new_cxxc->defun(old_fn);
        if (_do_inline) {
            new_fn.set_inline();
        }
    while (new_fn.replace_expr(ninputs_pat, ninputs_repl)) ;
    while (new_fn.replace_expr(noutputs_pat, noutputs_repl)) ;
    while (new_fn.replace_expr(push_pat, push_repl))
          any_push = true;
        #if HAVE_BATCH
        while (new_fn.replace_expr(push_batch_pat, push_batch_repl)) {
            any_push = true;
        }
        #endif
    while (new_fn.replace_expr(checked_push_pat, checked_push_repl))
      any_checked_push = true;
        #if HAVE_BATCH
    while (new_fn.replace_expr(checked_push_batch_pat, checked_push_batch_repl))
      any_checked_push = true;
    while (new_fn.replace_expr(sym_checked_push_batch_pat, sym_checked_push_batch_repl,true))
      any_checked_push = true;
        #endif
    while (new_fn.replace_expr(pull_pat, pull_repl))
      any_pull = true;
    }
    if (!any_push && !any_checked_push) {
        new_cxxc->find("output_push")->kill();
    #if HAVE_BATCH
        new_cxxc->find("output_push_batch")->kill();
    #endif
    }
    if (!any_checked_push) {
      new_cxxc->find("output_push_checked")->kill();
#if HAVE_BATCH
      new_cxxc->find("output_push_batch_checked")->kill();
#endif
    }
    if (!any_pull)
      new_cxxc->find("input_pull")->kill();
    }

  return true;
}

void
Specializer::do_simple_action(SpecializedClass &spc)
{
  CxxFunction *simple_action = spc.cxxc->find("simple_action");
  assert(simple_action);
  simple_action->kill();

  CxxFunction *smaction = spc.cxxc->find("smaction");
  if (smaction) {
    String r = smaction->ret_type().unshared().trim();
    if (r.starts_with("inline"))
      r = r.substring(6);
    r = r.remove(' ');
    if (r != "Packet*") {
      click_chatter("smaction has not a return type of Packet* (%s)", r.c_str());
      return;
    }
  } else {
    spc.cxxc->defun
      (CxxFunction("smaction", false, "inline Packet *", simple_action->args(),
          simple_action->body(), simple_action->clean_body()));
  }
  spc.cxxc->defun
    (CxxFunction("push", false, "void", "(int port, Packet *p)",
         "\n  if (Packet *q = smaction(p))\n\
    output_push(port, q);\n", ""));
  spc.cxxc->defun
    (CxxFunction("pull", false, "Packet *", "(int port)",
         "\n  Packet *p = input_pull(port);\n\
  return (p ? smaction(p) : 0);\n", ""));

#if HAVE_BATCH
  CxxFunction *simple_action_batch = spc.cxxc->find("simple_action_batch");
  if (!simple_action_batch) {
     //click_chatter("Auto-generating simple_action_batch for class %s", spc.old_click_name.c_str());
     spc.cxxc->defun
       (CxxFunction("smactionbatch", false, "inline PacketBatch *", "(PacketBatch *batch)",
         "EXECUTE_FOR_EACH_PACKET_DROPPABLE(smaction, batch, [](Packet*){});return batch;", ""));
  } else {
      simple_action_batch->kill();
      spc.cxxc->defun
        (CxxFunction("smactionbatch", false, "inline PacketBatch *", simple_action_batch->args(),
         simple_action_batch->body(), simple_action_batch->clean_body()));
  }

  spc.cxxc->defun
    (CxxFunction("push_batch", false, "inline void", "(int port, PacketBatch *batch)",
         "\n  if (PacketBatch *nbatch = smactionbatch(batch))\n\
    output_push_batch(port, nbatch);\n", ""));
  spc.cxxc->find("output_push_batch")->unkill();
#endif
  spc.cxxc->find("output_push")->unkill();
  spc.cxxc->find("input_pull")->unkill();
}

void
Specializer::unroll_run_task(SpecializedClass &spc) {
  CxxFunction *run_task = spc.cxxc->find("run_task");
  assert(run_task);
  run_task->kill();

  int unrolling_factor = (_unroll_val == 0) ? -1 : _unroll_val;

  /* Find the before/after the rte_eth_rx_burst for loop */
  String before_for = run_task->body().substring(
      0, run_task->body().find_left(
             "for", run_task->body().find_left("rte_eth_rx_burst")));
  String after_for = run_task->body().substring(
      run_task->body().find_left(
          "for", run_task->body().find_left("rte_eth_rx_burst")),
      run_task->body().length());

  /* Change 'n' with '_burst' */
  // TODO: if(n) and head->make_tail(last, n) is not replaced!
  bool found = false;
  if (run_task->replace_expr("n", "_burst", true)) {
    found = true;
  }

  if (found) {
    StringAccum new_body;
    String after_for_replaced = run_task->body().substring(
        run_task->body().find_left(
            "for", run_task->body().find_left("rte_eth_rx_burst")),
        run_task->body().length());
    new_body << before_for << "\n"
       << "if (likely(n == _burst)) { \n";
       if(unrolling_factor>0) {
       new_body << "#pragma GCC unroll " << unrolling_factor
       << "\n"
       //<< "#pragma clang loop unroll(enable)" << "\n"
       << "#pragma clang loop unroll_count(" << unrolling_factor << ")\n";
       }
       new_body << after_for_replaced << "\n"
       << "} else { \n"
       << after_for << "}\n";

    /* Change the body of the function */
    spc.cxxc->defun(CxxFunction(run_task->name(), false, run_task->ret_type(), run_task->args(), new_body.take_string(), " "));
  } else {
    click_chatter("Could not unroll the for loop in run_task!\n");
  }
}

void
Specializer::switch_run_task(SpecializedClass &spc) {
  CxxFunction *run_task = spc.cxxc->find("run_task");
  assert(run_task);
  run_task->kill();

  int n_switch_cases = _switch_burst;
  if(n_switch_cases == 0) {
     n_switch_cases = 32;
     click_chatter("switch pass changed the input burst to 32!\n");
  }

  /* Find the before/after the rte_eth_rx_burst for loop */

  int for_pos = run_task->body().find_left(
             "for", run_task->body().find_left("rte_eth_rx_burst"));
  int ret_pos = run_task->body().find_left(
             "return (ret)", run_task->body().find_left("rte_eth_rx_burst"));
  String before_for = run_task->body().substring(
      0, for_pos);
  String lambda_body = run_task->body().substring(
      for_pos, ret_pos - for_pos);

  String ret_clause = run_task->body().substring(
    ret_pos, run_task->body().length() );
    /* Define the lambda */
    StringAccum new_body;
    new_body << before_for << "\n"
      << "auto process_func = [&](unsigned n) { \n"
      << lambda_body << "};\n";

    /* Define the switch + cases */
     new_body << "switch (n) { \n";
      for(int i =n_switch_cases; i>=0;i--) {
        new_body << "case " << i << ":\n"
        << "process_func(" << i << ");\n"
        << ret_clause << "\n";
      }
      new_body << "}\n";


    /* Change the body of the function */
    spc.cxxc->defun(CxxFunction(run_task->name(), false, run_task->ret_type(), run_task->args(), new_body.take_string(), " "));
}

void
Specializer::computed_jmps_run_task(SpecializedClass &spc) {
  CxxFunction *run_task = spc.cxxc->find("run_task");
  assert(run_task);
  run_task->kill();

  int n_jmps_cases = _jmp_burst;
  if(n_jmps_cases == 0) {
     n_jmps_cases = 32;
     click_chatter("Computed jump pass changed the input burst to 32!\n");
  }

  /* Find the before/after the rte_eth_rx_burst for loop */

  int for_pos = run_task->body().find_left(
             "for", run_task->body().find_left("rte_eth_rx_burst"));
  int ret_pos = run_task->body().find_left(
             "return (ret)", run_task->body().find_left("rte_eth_rx_burst"));
  String before_for = run_task->body().substring(
      0, for_pos);
  String lambda_body = run_task->body().substring(
      for_pos, ret_pos - for_pos);

  String ret_clause = run_task->body().substring(
    ret_pos, run_task->body().length() );

    StringAccum new_body;
    new_body << before_for << "\n";

    /* Define the jump table */
    new_body << "static void* dispatch_table[] = {\n";
      for(int i =n_jmps_cases; i>=0;i--) {
        new_body << "&&burst_"<< i << ((i==0) ? "}" : " ,");
      }
      new_body << ";\n";

    /* Define the lambda function */
    new_body  << "auto process_func = [&](unsigned n) { \n"
      << lambda_body << "};\n"
      << "goto *dispatch_table["<<n_jmps_cases<<"-n];\n";

    /* Define the labels */
      for(int i =n_jmps_cases; i>=0;i--) {
        new_body << "burst_" << i << ":\n"
        << "process_func(" << i << ");\n"
        << ret_clause << "\n";
      }


    /* Change the body of the function */
    spc.cxxc->defun(CxxFunction(run_task->name(), false, run_task->ret_type(), run_task->args(), new_body.take_string(), " "));
}

inline const String &
Specializer::enew_cxx_type(int i) const
{
  int j = _specialize[i];
  return _specials[j].cxx_name;
}

void
Specializer::create_connector_methods(SpecializedClass &spc)
{
  assert(spc.cxxc);
  int eindex = spc.eindex;
  CxxClass *cxxc = spc.cxxc;

  // create mangled names of attached push and pull functions
  Vector<String> input_class(_ninputs[eindex], String());
  Vector<String> output_class(_noutputs[eindex], String());
  Vector<int> input_port(_ninputs[eindex], -1);
  Vector<int> output_port(_noutputs[eindex], -1);
  for (RouterT::conn_iterator it = _router->find_connections_from(_router->element(eindex));
       it != _router->end_connections(); ++it) {
      output_class[it->from_port()] = enew_cxx_type(it->to_eindex());
      output_port[it->from_port()] = it->to_port();
  }
  for (RouterT::conn_iterator it = _router->find_connections_to(_router->element(eindex));
       it != _router->end_connections(); ++it) {
      input_class[it->to_port()] = enew_cxx_type(it->from_eindex());
      input_port[it->to_port()] = it->from_port();
  }

  // create input_pull
  if (cxxc->find("input_pull")->alive()) {
    StringAccum sa;
    Vector<int> range1, range2;
    for (int i = 0; i < _ninputs[eindex]; i++)
      if (i > 0 && input_class[i] == input_class[i-1]
      && input_port[i] == input_port[i-1])
    range2.back() = i;
      else {
    range1.push_back(i);
    range2.push_back(i);
      }
    for (int i = 0; i < range1.size(); i++) {
      int r1 = range1[i], r2 = range2[i];
      if (!input_class[r1])
    continue;
      sa << "\n  ";
      if (r1 == r2)
    sa << "if (i == " << r1 << ") ";
      else
    sa << "if (i >= " << r1 << " && i <= " << r2 << ") ";
      if(!_do_static) {
        sa << "return ((" << input_class[r1] << " *)input(i).element())->"
        << input_class[r1] << "::pull(" << input_port[r1] << ");";
      } else {
        sa << "return obj_" << input_class[r1] << ".pull(" << input_port[r1]
        << ");";
      }
    }
    if (_ninputs[eindex])
    sa << "\n  return input(i).pull();\n";
    else
    sa << "\n  assert(0);\n  return 0;\n";
    cxxc->find("input_pull")->set_body(sa.take_string());
  }

  // create output_push
  if (cxxc->find("output_push")->alive()) {
    StringAccum sa;
    Vector<int> range1, range2;
    for (int i = 0; i < _noutputs[eindex]; i++)
      if (i > 0 && output_class[i] == output_class[i-1]
      && output_port[i] == output_port[i-1])
    range2.back() = i;
      else {
    range1.push_back(i);
    range2.push_back(i);
      }
    for (int i = 0; i < range1.size(); i++) {
      int r1 = range1[i], r2 = range2[i];
      if (!output_class[r1])
    continue;
      sa << "\n  ";
      if (r1 == r2)
    sa << "if (i == " << r1 << ") ";
      else
    sa << "if (i >= " << r1 << " && i <= " << r2 << ") ";
  if(!_do_static) {
    sa << "{ ((" << output_class[r1] << " *)output(i).element())->"
    << output_class[r1] << "::push(" << output_port[r1]
    << ", p); return; }";
  } else {
    sa << "{ obj_" << output_class[r1] << ".push(" << output_port[r1]
    << ", p); return; }";
  }
    }
    if (_noutputs[eindex])
    sa << "\n  output(i).push(p);\n";
    else
    sa << "\n  assert(0);\n";
    cxxc->find("output_push")->set_body(sa.take_string());

    sa.clear();
    if (_noutputs[eindex])
    sa << "\n  if (i < " << _noutputs[eindex] << ")\n"
       << "    output_push(i, p);\n  else\n    p->kill();\n";
    else
    sa << "\n  p->kill();\n";
    cxxc->find("output_push_checked")->set_body(sa.take_string());
  }

#if HAVE_BATCH
  // create output_push_batch
  click_chatter("Creating output_push_batch for %s",cxxc->name().c_str());
  if (cxxc->find("output_push_batch")->alive()) {
    StringAccum sa;
    Vector<int> range1, range2;
    for (int i = 0; i < _noutputs[eindex]; i++)
      if (i > 0 && output_class[i] == output_class[i-1]
      && output_port[i] == output_port[i-1])
    range2.back() = i;
      else {
    range1.push_back(i);
    range2.push_back(i);
      }
    for (int i = 0; i < range1.size(); i++) {
      int r1 = range1[i], r2 = range2[i];
      if (!output_class[r1])
    continue;
      sa << "\n  ";
      if (r1 == r2)
    sa << "if (i == " << r1 << ") ";
      else
    sa << "if (i >= " << r1 << " && i <= " << r2 << ") ";
  if(!_do_static) {
    sa << "{ ((" << output_class[r1] << " *)output(i).element())->"
    << output_class[r1] << "::push_batch(" << output_port[r1]
    << ", p); return; }";
  } else {
    sa << "{ obj_" << output_class[r1] << ".push_batch(" << output_port[r1]
    << ", p); return; }";
  }
    }
    if (_noutputs[eindex])
    sa << "\n  output(i).push_batch(p);\n";
    else
    sa << "\n  assert(0);\n";
    cxxc->find("output_push_batch")->set_body(sa.take_string());

    sa.clear();
    if (_noutputs[eindex])
    sa << "\n  if (i < " << _noutputs[eindex] << ")\n"
       << "    output_push_batch(i, p);\n  else\n    p->kill();\n";
    else
    sa << "\n  p->kill();\n";
    cxxc->find("output_push_batch_checked")->set_body(sa.take_string());
  } else {
      click_chatter("Push batch not alive");
  }
#endif
}

static
bool do_replacement(CxxFunction &fnt, CxxClass *cxxc, String from, String to, bool verbose) {
  if (!fnt.alive())
      return false;
  if (fnt.name() == "add_handlers" ||fnt.name() == "configure")
      return false;
  bool found = false;

  if (verbose)
    click_chatter("Replacing '%s' per '%s' in fnt %s",from.c_str(),to.c_str(), fnt.name().c_str());
  if (fnt.replace_expr(from, to, true, true)) {
      found = true;
      //click_chatter("CONST REPLACEMENT FOUND ! %s", fnt.body().c_str());
  }
  if (found)
     cxxc->defun(fnt);
  return found;
}

static String
trim_quotes(String s) {
    int b = 0;
    int e = s.length() - 1;
    while (b <= e && s[b] == '"') {
        b++;
    }
    while (e >=b && s[e] == '"') {
        e--;
    }
    return s.substring(b, e);
}

bool
is_primitive_val(String str) {
    str = str.trim().lower();
    if (str == "true" || str == "false")
        return true;
    bool minus = false;
    for (int i = 0; i < str.length(); i++) {
        if (!isdigit(str[i])) {
            if (str[i] == '-' && !minus)
                minus = true;
            else
                return false;
        }
    }
    return true;
}

void
Specializer::specialize(const Signatures &sigs, ErrorHandler *errh)
{
  // decide what is to be specialized
  _specialize = sigs.signature_ids();
  SpecializedClass spc;
  spc.eindex = SPCE_NOT_DONE;
  _specials.assign(sigs.nsignatures(), spc);
  _specials[0].eindex = SPCE_NOT_SPECIAL;
  for (int i = 0; i < _nelements; i++) {
    click_chatter("Element %s",_router->element(i)->name().c_str());
    check_specialize(i, errh);
  }

  // actually do the work
  for (int s = 0; s < _specials.size(); s++) {
    if (create_class(_specials[s])) {

      /* Unroll the for loop after rte_eth_rx_burst in FromDPDKDevice */
      if (_do_unroll) {
          //_specials[s].cxxc->print_function_list();
          if (_specials[s].cxxc->find("run_task")) {
                        if(_specials[s].cxxc->name().find_left("FromDPDKDevice")>=0) {
                          unroll_run_task(_specials[s]);
                        }
                      }
      } else if (_do_switch) {
        /* Duplicate the loop for different switch-cases */
       if (_specials[s].cxxc->find("run_task")) {
                        if(_specials[s].cxxc->name().find_left("FromDPDKDevice")>=0) {
                          switch_run_task(_specials[s]);
                        }
                      }
      } else if (_do_jmps) {
        /* Duplicate the loop for different computed jmp labels */
       if (_specials[s].cxxc->find("run_task")) {
                        if(_specials[s].cxxc->name().find_left("FromDPDKDevice")>=0) {
                          computed_jmps_run_task(_specials[s]);
                        }
                      }
      }
      if (_specials[s].cxxc->find("simple_action") || _specials[s].cxxc->find("simple_action_batch"))
        do_simple_action(_specials[s]);

    }
  }

  for (int s = 0; s < _specials.size(); s++)
    if (_specials[s].special())
      create_connector_methods(_specials[s]);

  //Replace the parameters in configure() functions
  do_config_replacement();
}


/**
 * Replace argument parsing by constant value validation, and replace occurences
 * of the symbols by their constants
 */
void
Specializer::do_config_replacement() {
  for (int s = 0; s < _specials.size(); s++) {

    if (!_specials[s].special())
        continue;
    CxxClass* original = _specials[s].cxxc->parent(0);

    if (_verbose)
        click_chatter("Replacing in %s", _specials[s].cxxc->name().c_str());

    CxxFunction* configure = original->find("configure");
    if (configure && _do_replace) {
        bool any_replacement = false;// Replacements in configure done?
        Vector<String> args;

        String configline = _router->element(_specials[s].eindex)->config();
        //For all patterns like read, read_mp, ...
        for (int p =0; p < patterns.size(); p++) {
                int res;
                while ((res = configure->replace_call(patterns[p].first, patterns[p].second, args)) != -1) {
                      String value;
                      String param =  trim_quotes(args[0].trim());
                      bool has_value;
                      any_replacement = true;
                      int pos = configline.find_left(param);
                      int endofrep = res;
                      bool dynamic = false;
                      bool fromcode = false;
                      String symbol = args[1].trim();
                      if (pos >= 0) {
                          //Value given by the user
                          if (_verbose)
                              click_chatter("Found user value for %s", param.c_str());
                          pos += param.length();
                          while (configline[pos] == ' ')
                              pos++;
                          int end = pos + 1;
                          while (configline[end] != ',' && configline[end] != ')') end++;
                          end -= 1;
                          while (configline[end] == ' ') end--;
                          value = configline.substring(pos,end+1 - pos).trim();
                          if (value.starts_with("!")) {
                            dynamic = true;
                            pos = value.data() - configline.data();
                            configline = configline.substring(0,pos) + configline.substring(pos + 1);
                            continue;
                          }

                          click_chatter("Config value is %s",value.c_str());
                          if (value.starts_with("$")) {
                              click_chatter("Variables not yet supported");
                              has_value = false;
                              dynamic = true;
                          } else {
                              has_value = true;
                          }
                      } else {
                          //Value not given by the user
                          if (args.size() > 2 && args[2].trim()) {
                              value=args[2].trim();
                              fromcode = true;
                              if (_verbose)
                                  click_chatter("User did not overwrite %s, replacing by default value %s", param.c_str(), value.c_str());
                              has_value = true;
                          } else {
                              value = configure->find_assignment(symbol,res-patterns[p].second.length());
                              if (value) {
                                  has_value = true;
                              } else {
                                  if (_verbose)
                                      click_chatter("User did not overwrite %s, preventing further overwrite", param.c_str());
                                  has_value = false;
                              }
                          }

                      }
                      if (has_value) {
                          if (_verbose)
                              click_chatter("Value %s given, primitive : %d",value.c_str(),is_primitive_val(value));
                          configure->replace_expr("!TEMPVAL!", ", "+symbol+", "+ (is_primitive_val(value)||fromcode?value:"\""+value+"\""), false, true);
                      } else if (!dynamic) {
                          click_chatter("No value given");
                          configure->replace_expr("!TEMPVAL!", "", false);
                      } else {
                          configure->replace_expr("!TEMPVAL!", ", true", false);
                      }

                      if (!has_value || !is_primitive_val(value)) {
                          args.clear();
                          continue;
                      }

                      //Replace in specialized code
                      for (int f = 0; f < _specials[s].cxxc->nfunctions(); f++) {
                          CxxFunction &fnt = _specials[s].cxxc->function(f);
                          do_replacement(fnt, _specials[s].cxxc, symbol, value, _verbose);
                      }

                      //Replace in original class code
                      for (int f = 0; f < original->nfunctions(); f++) {
                          CxxFunction &fnt = original->function(f);
                          if (&fnt == configure)
                              continue;
                          CxxFunction *overriden = 0;
                          if ((overriden = _specials[s].cxxc->find(fnt.name())))
                              fnt = *overriden;
                          do_replacement(fnt, _specials[s].cxxc, symbol, value, _verbose);
                      }

                      //Replace in the configure function itself
                      if (configure->replace_expr(symbol, value, true, true, endofrep)) {

                      }

                      args.clear();
                }
                if (any_replacement) {

                    _specials[s].cxxc->defun(*configure);
                }
                //click_chatter("Configure changed  %s!",configure->body().c_str());
        }
    }
  }
}

void
Specializer::fix_elements()
{
  for (int i = 0; i < _nelements; i++) {
    SpecializedClass &spc = _specials[ _specialize[i] ];
    if (spc.special())
      _router->element(i)->set_type(ElementClassT::base_type(spc.click_name));
  }
}

void
Specializer::output_includes(ElementTypeInfo &eti, StringAccum &out)
{
  // don't write includes twice for the same class
  if (eti.wrote_includes)
    return;

  // must massage includes.
  // we may have something like '#include "element.hh"', relying on the
  // assumption that we are compiling 'element.cc'. must transform this
  // to '#include "path/to/element.hh"'.
  // XXX this is probably not the best way to do this
  const String &includes = eti.includes;
  const char *s = includes.data();
  int len = includes.length();

  // skip past '#ifndef X\n#define X' (sort of)
  int p = 0;
  while (p < len && isspace((unsigned char) s[p]))
    p++;
  if (len - p > 7 && strncmp(s + p, "#ifndef", 7) == 0) {
    int next = p + 7;
    for (; next < len && s[next] != '\n'; next++)
      /* nada */;
    if (next + 8 < len && strncmp(s + next + 1, "#define", 7) == 0) {
      for (p = next + 8; p < len && s[p] != '\n'; p++)
    /* nada */;
    }
  }

  // now collect includes
  while (p < len) {
    int start = p;
    int p2 = p;
    while (p2 < len && s[p2] != '\n' && s[p2] != '\r')
      p2++;
    while (p < p2 && isspace((unsigned char) s[p]))
      p++;

    if (p < p2 && s[p] == '#') {
      // we have a preprocessing directive!

      // skip space after '#'
      for (p++; p < p2 && isspace((unsigned char) s[p]); p++)
    /* nada */;

      // check for '#include'
      if (p + 7 < p2 && strncmp(s+p, "include", 7) == 0) {

    // find what is "#include"d
    for (p += 7; p < p2 && isspace((unsigned char) s[p]); p++)
      /* nada */;

    // interested in "user includes", not <system includes>
    if (p < p2 && s[p] == '\"') {
      int left = p + 1;
      for (p++; p < p2 && s[p] != '\"'; p++)
        /* nada */;
      String include = includes.substring(left, p - left);
      int include_index = _header_file_map.get(include);
      if (include_index >= 0) {
        if (!_etinfo[include_index].found_header_file)
          _etinfo[include_index].locate_header_file(_router, ErrorHandler::default_handler());
        out << "#include \"" << _etinfo[include_index].found_header_file << "\"\n";
        p = p2 + 1;
        continue;        // don't use previous #include text
      } else if (left + 1 < p && s[left] != '/' && eti.found_header_file) {
          const char *fhf_begin = eti.found_header_file.begin();
          const char *fhf_end = eti.found_header_file.end();
          while (fhf_begin < fhf_end && fhf_end[-1] != '/')
          fhf_end--;
          if (fhf_begin < fhf_end) {
          out << "#include \"" << eti.found_header_file.substring(fhf_begin, fhf_end) << include << "\"\n";
          p = p2 + 1;
          continue;    // don't use previous #include text
          }
      }
    }
      }

    }

    out << includes.substring(start, p2 + 1 - start);
    p = p2 + 1;
  }

  eti.wrote_includes = true;
}

void
Specializer::output(StringAccum& out_header, StringAccum& out)
{
  // output headers
  for (int i = 0; i < _specials.size(); i++) {
    SpecializedClass &spc = _specials[i];
    if (spc.eindex >= 0) {
      ElementTypeInfo &eti = etype_info(spc.eindex);
      if (eti.found_header_file)
    out_header << "#include \"" << eti.found_header_file << "\"\n";
      if (spc.special())
    spc.cxxc->header_text(out_header, _do_align);
    }
  }

  // output C++ code
  for (int i = 0; i < _specials.size(); i++) {
    SpecializedClass &spc = _specials[i];
    if (spc.special()) {
      ElementTypeInfo &eti = etype_info(spc.eindex);
      output_includes(eti, out);
      spc.cxxc->source_text(out);
    }
  }
}

void
Specializer::output_package(const String &package_name, const String &suffix, StringAccum &out, ErrorHandler* errh)
{
    StringAccum elem2package, cmd_sa;
    for (int i = 0; i < _specials.size(); i++)
    if (_specials[i].special())
        elem2package <<  "-\t\"" << package_name << suffix << ".hh\"\t" << _specials[i].cxx_name << '-' << _specials[i].click_name << '\n';
    String click_buildtool_prog = clickpath_find_file("click-buildtool", "bin", CLICK_BINDIR, errh);
    if (!_do_static) {
      cmd_sa << click_buildtool_prog << " elem2package " << package_name;
    } else {
      cmd_sa << click_buildtool_prog << " elem2packagestatic " << package_name;
    }
    out << shell_command_output_string(cmd_sa.take_string(), elem2package.take_string(), errh);
}

void
Specializer::output_new_elementmap(const ElementMap &full_em, ElementMap &em,
                   const String &filename, const String &requirements) const
{
    for (int i = 0; i < _specials.size(); i++)
    if (_specials[i].special()) {
        Traits e = full_em.traits(_specials[i].old_click_name);
        e.name = _specials[i].click_name;
        e.cxx = _specials[i].cxx_name;
        e.header_file = filename + ".hh";
        e.source_file = filename + ".cc";
        e.requirements = requirements + _specials[i].old_click_name;
        e.provisions = String();
        em.add(e);
    }
}
