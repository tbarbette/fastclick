// -*- c-basic-offset: 4 -*-
/*
 * click.cc -- user-level Click main program
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2000 Mazu Networks, Inc.
 * Copyright (c) 2001-2003 International Computer Science Institute
 * Copyright (c) 2004-2006 Regents of the University of California
 * Copyright (c) 2008-2009 Meraki, Inc.
 * Copyright (c) 1999-2015 Eddie Kohler
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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#if HAVE_EXECINFO_H
# include <execinfo.h>
#endif

#ifdef HAVE_DPDK
# include <rte_common.h>
# include <rte_eal.h>
# include <rte_lcore.h>
# include <rte_version.h>
#endif // HAVE_DPDK

#include <click/clp.h>
#include <click/lexer.hh>
#include <click/routerthread.hh>
#include <click/router.hh>
#include <click/master.hh>
#include <click/timer.hh>
#include <click/straccum.hh>
#include <click/archive.hh>
#include <click/glue.hh>
#include <click/driver.hh>
#include <click/userutils.hh>
#include <click/handlercall.hh>
#include "elements/standard/quitwatcher.hh"
#include "elements/userlevel/controlsocket.hh"
CLICK_USING_DECLS

static Master* click_master;
static Router* click_router;
static bool running = false;
static ErrorHandler* errh;

static int
cleanup(Clp_Parser *clp, int exit_value)
{
    Clp_DeleteParser(clp);
    click_static_cleanup();
    delete click_master;
    return exit_value;
}


#include "args.hh"

extern "C" {
static void
stop_signal_handler(int sig)
{
#if !HAVE_SIGACTION
    signal(sig, SIG_DFL);
#endif
    if (!running)
        kill(getpid(), sig);
    else
        click_router->set_runcount(Router::STOP_RUNCOUNT);
}

#if HAVE_EXECINFO_H
static void
catch_dump_signal(int sig)
{
    (void) sig;

    /* reset these signals so if we do something bad we just exit */
    click_signal(SIGSEGV, SIG_DFL, false);
    click_signal(SIGBUS, SIG_DFL, false);
    click_signal(SIGILL, SIG_DFL, false);
    click_signal(SIGABRT, SIG_DFL, false);
    click_signal(SIGFPE, SIG_DFL, false);

    /* dump the results to standard error */
    void *return_addrs[50];
    int naddrs = backtrace(return_addrs, sizeof(return_addrs) / sizeof(void *));
    backtrace_symbols_fd(return_addrs, naddrs, STDERR_FILENO);

    /* dump core and quit */
    abort();
}
#endif
}


// report handler results

static int
call_read_handler(Element *e, String handler_name,
                  bool print_name, ErrorHandler *errh)
{
  const Handler *rh = Router::handler(e, handler_name);
  String full_name = Handler::unparse_name(e, handler_name);
  if (!rh || !rh->visible())
    return errh->error("no %<%s%> handler", full_name.c_str());
  else if (!rh->read_visible())
    return errh->error("%<%s%> is a write handler", full_name.c_str());

  if (print_name)
    fprintf(stdout, "%s:\n", full_name.c_str());
  String result = rh->call_read(e);
  if (!rh->raw() && result && result.back() != '\n')
      result += '\n';
  fputs(result.c_str(), stdout);
  if (print_name)
    fputs("\n", stdout);

  return 0;
}

static int
expand_handler_elements(const String& pattern, const String& handler_name,
                        Vector<Element*>& elements, Router* router)
{
    // first try element name
    if (Element* e = router->find(pattern)) {
        elements.push_back(e);
        return 1;
    }
    // check if we have a pattern
    bool is_pattern = false;
    for (const char* s = pattern.begin(); s < pattern.end(); s++)
        if (*s == '?' || *s == '*' || *s == '[') {
            is_pattern = true;
            break;
        }
    // check pattern or type
    bool any = false;
    for (int i = 0; i < router->nelements(); i++)
        if (is_pattern
            ? glob_match(router->ename(i), pattern)
            : router->element(i)->cast(pattern.c_str()) != 0) {
            any = true;
            const Handler* h = Router::handler(router->element(i), handler_name);
            if (h && h->read_visible())
                elements.push_back(router->element(i));
        }
    if (!any)
        return errh->error((is_pattern ? "no element matching %<%s%>" : "no element %<%s%>"), pattern.c_str());
    else
        return 2;
}

static int
call_read_handlers(Vector<String> &handlers, ErrorHandler *errh)
{
    Vector<Element *> handler_elements;
    Vector<String> handler_names;
    bool print_names = (handlers.size() > 1);
    int before = errh->nerrors();

    // expand handler names
    for (int i = 0; i < handlers.size(); i++) {
        const char *dot = find(handlers[i], '.');
        if (dot == handlers[i].end()) {
            call_read_handler(click_router->root_element(), handlers[i],
                              print_names, errh);
            continue;
        }

        String element_name = handlers[i].substring(handlers[i].begin(), dot);
        String handler_name = handlers[i].substring(dot + 1, handlers[i].end());

        Vector<Element*> elements;
        int retval = expand_handler_elements(element_name, handler_name,
                                             elements, click_router);
        if (retval >= 0)
            for (int j = 0; j < elements.size(); j++)
                call_read_handler(elements[j], handler_name,
                                  print_names || retval > 1, errh);
    }

    return (errh->nerrors() == before ? 0 : -1);
}


// hotswapping

static Router* hotswap_router;
static Router* hotswap_thunk_router;
static bool hotswap_hook(Task *, void *);
static Task hotswap_task(hotswap_hook, 0);

static bool
hotswap_hook(Task*, void*)
{
    hotswap_thunk_router->set_foreground(false);
    hotswap_router->activate(ErrorHandler::default_handler());
    click_router->unuse();
    click_router = hotswap_router;
    click_router->use();
    hotswap_router = 0;
    return true;
}

#if HAVE_MULTITHREAD
static pthread_mutex_t hotswap_lock;

extern "C" {
static void* hotswap_threadfunc(void*)
{
    pthread_detach(pthread_self());
# if HAVE_CLICK_PACKET_POOL
    WritablePacket::initialize_local_packet_pool();
# endif
    pthread_mutex_lock(&hotswap_lock);
    if (hotswap_router) {
        click_master->block_all();
        hotswap_hook(0, 0);
        click_master->unblock_all();
    }
    pthread_mutex_unlock(&hotswap_lock);
    return 0;
}
}
#endif

static String
click_driver_control_socket_name(int number)
{
    if (!number)
        return "click_driver@@ControlSocket";
    else
        return "click_driver@@ControlSocket@" + String(number);
}

static Router *
parse_configuration(const String &text, bool text_is_expr, bool hotswap, click_args_t &args,
                    ErrorHandler *errh)
{
    int before_errors = errh->nerrors();
    Router *router = click_read_router(text, text_is_expr, errh, false,
                                       click_master);
    if (!router)
        return 0;

    // add new ControlSockets
    String retries = (hotswap ? ", RETRIES 1, RETRY_WARNINGS false" : "");
    int ncs = 0;
    for (String *it = args.cs_ports.begin(); it != args.cs_ports.end(); ++it, ++ncs)
        router->add_element(new ControlSocket, click_driver_control_socket_name(ncs), "TCP, " + *it + retries, "click", 0);
    for (String *it = args.cs_unix_sockets.begin(); it != args.cs_unix_sockets.end(); ++it, ++ncs)
        router->add_element(new ControlSocket, click_driver_control_socket_name(ncs), "UNIX, " + *it + retries, "click", 0);
    for (String *it = args.cs_sockets.begin(); it != args.cs_sockets.end(); ++it, ++ncs)
        router->add_element(new ControlSocket, click_driver_control_socket_name(ncs), "SOCKET, " + *it + retries, "click", 0);

  // catch signals (only need to do the first time)
  if (!hotswap) {
      // catch control-C and SIGTERM
      click_signal(SIGINT, stop_signal_handler, true);
      click_signal(SIGTERM, stop_signal_handler, true);
      // ignore SIGPIPE
      click_signal(SIGPIPE, SIG_IGN, false);

#if HAVE_EXECINFO_H
    const char *click_backtrace = getenv("CLICK_BACKTRACE");
    bool do_click_backtrace;
    if (click_backtrace && (!BoolArg().parse(click_backtrace, do_click_backtrace)
                            || do_click_backtrace)) {
        click_signal(SIGSEGV, catch_dump_signal, false);
        click_signal(SIGBUS, catch_dump_signal, false);
        click_signal(SIGILL, catch_dump_signal, false);
        click_signal(SIGABRT, catch_dump_signal, false);
        click_signal(SIGFPE, catch_dump_signal, false);
    }
#endif
  }

  // register hotswap router on new router
  if (hotswap && click_router && click_router->initialized())
      router->set_hotswap_router(click_router);

  if (errh->nerrors() == before_errors
      && router->initialize(errh) >= 0)
    return router;
  else {
    delete router;
    return 0;
  }
}

static int
hotconfig_handler(const String &text, Element *, void *args, ErrorHandler *errh)
{
  if (Router *new_router = parse_configuration(text, true, true, *(click_args_t*)args, errh)) {
#if HAVE_MULTITHREAD
      pthread_mutex_lock(&hotswap_lock);
#endif
      if (hotswap_router)
          hotswap_router->unuse();
      hotswap_router = new_router;
      hotswap_thunk_router->set_foreground(true);
#if HAVE_MULTITHREAD
      pthread_t thread_ignored;
      pthread_create(&thread_ignored, 0, hotswap_threadfunc, 0);
      (void) thread_ignored;
      pthread_mutex_unlock(&hotswap_lock);
#else
      hotswap_task.reschedule();
#endif
      return 0;
  } else
      return -EINVAL;
}


#ifdef TIMESTAMP_WARPABLE
// timewarping

static String
timewarp_read_handler(Element *, void *)
{
    if (Timestamp::warp_class() == Timestamp::warp_simulation)
        return "simulation";
    else if (Timestamp::warp_class() == Timestamp::warp_nowait)
        return "nowait";
    else
        return String(Timestamp::warp_speed());
}

static int
timewarp_write_handler(const String &text, Element *, void *, ErrorHandler *errh)
{
    if (text == "nowait")
        Timestamp::warp_set_class(Timestamp::warp_nowait);
    else {
        double factor;
        if (!DoubleArg().parse(text, factor))
            return errh->error("expected double");
        else if (factor <= 0)
            return errh->error("timefactor must be > 0");
        Timestamp::warp_set_class(Timestamp::warp_linear, factor);
    }
    return 0;
}
#endif

// main

static void
round_timeval(struct timeval *tv, int usec_divider)
{
    tv->tv_usec = (tv->tv_usec + usec_divider / 2) / usec_divider;
    if (tv->tv_usec >= 1000000 / usec_divider) {
        tv->tv_usec = 0;
        ++tv->tv_sec;
    }
}

#if HAVE_MULTITHREAD
extern "C" {
static void *thread_driver(void *user_data)
{
    RouterThread *thread = static_cast<RouterThread *>(user_data);
    thread->driver();
    return 0;
}
# if HAVE_DPDK
static int thread_driver_dpdk(void *user_data) {
    RouterThread *thread = static_cast<RouterThread *>(user_data);
    thread->driver();
    return 0;
}
# endif
}
#endif

#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
void do_set_affinity(pthread_t p, int cpu, click_args_t &args) {
    if (!dpdk_enabled && args.click_affinity_offset >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu + args.click_affinity_offset, &set);
        pthread_setaffinity_np(p, sizeof(cpu_set_t), &set);
    }
}
#else
# define do_set_affinity(p, cpu, args) /* nothing */
#endif

int
main(int argc, char **argv)
{
  click_static_initialize();

  errh = ErrorHandler::default_handler();

  click_args_t args;
  int ret = parse(argc, argv, args);
  if (ret != 0)
      return ret;

#if HAVE_DPDK
  if (!dpdk_enabled) {
#if CLICK_PACKET_USE_DPDK
        dpdk_enabled = true;
        args.dpdk_arg.push_back((char*)(new String("--no-huge"))->c_str());
        args.dpdk_arg.push_back((char*)(new String("-l"))->c_str());
        char* s = (char*)malloc(14);
        if (args.click_affinity_offset < 0)
            args.click_affinity_offset = 0;
        snprintf(s,14,"%d-%d", args.click_affinity_offset, args.click_affinity_offset + click_nthreads - 1);
        args.dpdk_arg.push_back(s);

        args.dpdk_arg.push_back((char*)(new String("-m"))->c_str());

        args.dpdk_arg.push_back((char*)(new String("512M"))->c_str());

        args.dpdk_arg.push_back((char*)(new String("--log-level=1"))->c_str());

        args.dpdk_arg.push_back((char*)(new String("--"))->c_str());
#ifdef HAVE_VERBOSE_BATCH
        click_chatter("ERROR: Click was compiled with --enable-dpdk-packet, and must therefore be launched with the '--dpdk --' arguments. We'll try to run with --dpdk %s %s %s -m 512M -v --log-level=debug -- but it's likely to fail... This is only to allow automatic testing.",args.dpdk_arg[1],args.dpdk_arg[2], args.dpdk_arg[3]);
#endif
#endif
    }
    if (dpdk_enabled) {
#ifdef HAVE_VERBOSE_BATCH
        if (click_nthreads > 1)
            errh->warning("In DPDK mode, set the number of cores with DPDK EAL arguments");
# if HAVE_DECL_PTHREAD_SETAFFINITY_NP
        if (args.click_affinity_offset >= 0)
            errh->warning("In DPDK mode, set core affinity with DPDK EAL arguments");
# endif
#endif
        int n_eal_args = rte_eal_init(args.dpdk_arg.size(), args.dpdk_arg.data());
        if (n_eal_args < 0)
            rte_exit(EXIT_FAILURE,
                     "Click was built with DPDK support but there was an\n"
                     "          error parsing the EAL arguments or launching DPDK EAL.\n");
        click_nthreads = rte_lcore_count();
     }
#endif

  // provide hotconfig handler if asked
  if (args.allow_reconfigure)
      Router::add_write_handler(0, "hotconfig", hotconfig_handler, &args, Handler::f_raw | Handler::f_nonexclusive);

#ifdef TIMESTAMP_WARPABLE
  Router::add_read_handler(0, "timewarp", timewarp_read_handler, 0);
  if (Timestamp::warp_class() != Timestamp::warp_simulation)
      Router::add_write_handler(0, "timewarp", timewarp_write_handler, 0);
#endif
  // parse configuration
  click_master = new Master(click_nthreads);
  click_router = parse_configuration(args.router_file, args.file_is_expr, false, args, errh);
  if (!click_router)
    return cleanup(args.clp, 1);
  click_router->use();

  int exit_value = 0;
#if (HAVE_MULTITHREAD)
  Vector<pthread_t> other_threads;
  if (!dpdk_enabled)
      pthread_mutex_init(&hotswap_lock, 0);
#endif

  // output flat configuration
  if (args.output_file) {
    FILE *f = 0;
    if (strcmp(args.output_file, "-") != 0) {
      f = fopen(args.output_file, "w");
      if (!f) {
        errh->error("%s: %s", args.output_file, strerror(errno));
        exit_value = 1;
      }
    } else
      f = stdout;
    if (f) {
      Element *root = click_router->root_element();
      String s = Router::handler(root, "flatconfig")->call_read(root);
      ignore_result(fwrite(s.data(), 1, s.length(), f));
      if (f != stdout)
        fclose(f);
    }
  }

  struct rusage before, after;
  getrusage(RUSAGE_SELF, &before);

#ifdef TIMESTAMP_WARPABLE
  Timestamp before_time = Timestamp::now_unwarped();
#else
  Timestamp before_time = Timestamp::now();
#endif
  Timestamp after_time = Timestamp::uninitialized_t();

  // run driver
  // 10.Apr.2004 - Don't run the router if it has no elements.
  if (!args.quit_immediately && click_router->nelements()) {
    running = true;
    click_router->activate(errh);
    if (args.allow_reconfigure) {
      hotswap_thunk_router = new Router("", click_master);
      hotswap_thunk_router->initialize(errh);
      hotswap_task.initialize(hotswap_thunk_router->root_element(), false);
      hotswap_thunk_router->activate(false, errh);
    }
    for (int t = 0; t < click_nthreads; ++t)
        click_master->thread(t)->mark_driver_entry();
#if HAVE_MULTITHREAD
# if HAVE_DPDK
    if (dpdk_enabled) {
        unsigned t = 1;
        unsigned lcore_id;
#   if RTE_VERSION >= RTE_VERSION_NUM(20,11,0,0)
        RTE_LCORE_FOREACH_WORKER(lcore_id) {
#   else
        RTE_LCORE_FOREACH_SLAVE(lcore_id) {
#   endif
            rte_eal_remote_launch(thread_driver_dpdk, click_router->master()->thread(t++),
                                  lcore_id);
        }
    } else
# endif //HAVE_DPDK
    {
        for (int t = 1; t < click_nthreads; ++t) {
            pthread_t p;
            pthread_create(&p, 0, thread_driver, click_master->thread(t));
            other_threads.push_back(p);
            do_set_affinity(p, t, args);
        }
        do_set_affinity(pthread_self(), 0, args);
    }
#endif

    // run driver
    click_master->thread(0)->driver();

    // now that the driver has stopped, SIGINT gets default handling
    running = false;
    click_fence();
  } else if (!args.quit_immediately && args.warnings)
    errh->warning("%s: configuration has no elements, exiting", filename_landmark(args.router_file, args.file_is_expr));

#ifdef TIMESTAMP_WARPABLE
  after_time.assign_now_unwarped();
#else
  after_time.assign_now();
#endif
  getrusage(RUSAGE_SELF, &after);
  // report time
  if (args.report_time) {
    struct timeval diff;
    timersub(&after.ru_utime, &before.ru_utime, &diff);
    round_timeval(&diff, 1000);
    printf("%ld.%03ldu", (long)diff.tv_sec, (long)diff.tv_usec);
    timersub(&after.ru_stime, &before.ru_stime, &diff);
    round_timeval(&diff, 1000);
    printf(" %ld.%03lds", (long)diff.tv_sec, (long)diff.tv_usec);
    diff = (after_time - before_time).timeval();
    round_timeval(&diff, 10000);
    printf(" %ld:%02ld.%02ld", (long)(diff.tv_sec/60), (long)(diff.tv_sec%60), (long)diff.tv_usec);
    printf("\n");
  }

  // call handlers
  if (args.handlers.size())
    if (call_read_handlers(args.handlers, errh) < 0)
      exit_value = 1;

  // call exit handler
  if (args.exit_handler) {
    int before = errh->nerrors();
    String exit_string = HandlerCall::call_read(args.exit_handler, click_router->root_element(), errh);
    bool b;
    if (errh->nerrors() != before)
      exit_value = -1;
    else if (IntArg().parse(cp_uncomment(exit_string), exit_value))
      /* nada */;
    else if (BoolArg().parse(cp_uncomment(exit_string), b))
      exit_value = (b ? 0 : 1);
    else {
      errh->error("exit handler value should be integer");
      exit_value = -1;
    }
  }

#if HAVE_MULTITHREAD
# if HAVE_DPDK
  if (dpdk_enabled) {
      rte_eal_mp_wait_lcore();
      goto click_cleanup;
  }
# endif

  for (int i = 0; i < other_threads.size(); ++i)
      click_master->thread(i + 1)->wake();
  for (int i = 0; i < other_threads.size(); ++i)
      (void) pthread_join(other_threads[i], 0);
#endif

#if HAVE_MULTITHREAD && HAVE_DPDK
click_cleanup:
#endif
  click_router->unuse();
  return cleanup(args.clp, exit_value);
}
