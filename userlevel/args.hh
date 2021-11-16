#include <click/clp.h>
#include <click/error.hh>
#include <click/args.hh>
#include <click/lexer.hh>
#include <click/driver.hh>
#include <click/userutils.hh>

#define HELP_OPT                300
#define VERSION_OPT             301
#define CLICKPATH_OPT           302
#define ROUTER_OPT              303
#define EXPRESSION_OPT          304
#define QUIT_OPT                305
#define OUTPUT_OPT              306
#define HANDLER_OPT             307
#define TIME_OPT                308
#define PORT_OPT                310
#define UNIX_SOCKET_OPT         311
#define NO_WARNINGS_OPT         312
#define WARNINGS_OPT            313
#define ALLOW_RECONFIG_OPT      314
#define EXIT_HANDLER_OPT        315
#define THREADS_OPT             316
#define SIMTIME_OPT             317
#define SOCKET_OPT              318
#define THREADS_AFF_OPT         319
#define DPDK_OPT                320
#define SIMTICK_OPT             321

static const Clp_Option options[] = {
    { "allow-reconfigure", 'R', ALLOW_RECONFIG_OPT, 0, Clp_Negate },
    { "clickpath", 'C', CLICKPATH_OPT, Clp_ValString, 0 },
    { "expression", 'e', EXPRESSION_OPT, Clp_ValString, 0 },
    { "dpdk", 0, DPDK_OPT, 0, 0 },
    { "file", 'f', ROUTER_OPT, Clp_ValString, 0 },
    { "handler", 'h', HANDLER_OPT, Clp_ValString, 0 },
    { "help", 0, HELP_OPT, 0, 0 },
    { "output", 'o', OUTPUT_OPT, Clp_ValString, 0 },
    { "socket", 0, SOCKET_OPT, Clp_ValInt, 0 },
    { "port", 'p', PORT_OPT, Clp_ValString, 0 },
    { "quit", 'q', QUIT_OPT, 0, 0 },
    { "simtime", 0, SIMTIME_OPT, Clp_ValDouble, Clp_Optional },
    { "simulation-time", 0, SIMTIME_OPT, Clp_ValDouble, Clp_Optional },
    { "simtick", 0, SIMTICK_OPT, Clp_ValUnsignedLong, Clp_Mandatory },
    { "threads", 'j', THREADS_OPT, Clp_ValInt, 0 },
    { "cpu", 0, THREADS_AFF_OPT, Clp_ValInt, Clp_Optional | Clp_Negate },
    { "affinity", 'a', THREADS_AFF_OPT, Clp_ValInt, Clp_Optional | Clp_Negate },
    { "time", 't', TIME_OPT, 0, 0 },
    { "unix-socket", 'u', UNIX_SOCKET_OPT, Clp_ValString, 0 },
    { "version", 'v', VERSION_OPT, 0, 0 },
    { "warnings", 0, WARNINGS_OPT, 0, Clp_Negate },
    { "exit-handler", 'x', EXIT_HANDLER_OPT, Clp_ValString, 0 },
    { 0, 'w', NO_WARNINGS_OPT, 0, Clp_Negate },
};

static const char *program_name;

// switching configurations
int click_nthreads = 1;
bool dpdk_enabled = false;


void
short_usage()
{
  fprintf(stderr, "Usage: %s [OPTION]... [ROUTERFILE]\n\
Try '%s --help' for more information.\n",
          program_name, program_name);
}

void
usage()
{
    printf("\
'Click' runs a Click router configuration at user level. It installs the\n\
configuration, reporting any errors to standard error, and then generally runs\n\
until interrupted.\n\
\n\
Usage: %s [OPTION]... [ROUTERFILE]\n\
\n\
Options:\n\
  -f, --file FILE               Read router configuration from FILE.\n\
  -e, --expression EXPR         Use EXPR as router configuration.\n\
  -j, --threads N               Start N threads (default 1).\n", program_name);
#if HAVE_DPDK
    printf("\
      --dpdk DPDK_ARGS --       Enable DPDK and give DPDK's own arguments.\n");
#endif
#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
    printf("\
  -a, --affinity[=N]            Pin threads to CPUs starting at #N (default 0).\n");
#endif
    printf("\
  -p, --port PORT               Listen for control connections on TCP port.\n\
  -u, --unix-socket FILE        Listen for control connections on Unix socket.\n\
      --socket FD               Add a file descriptor control connection.\n\
  -R, --allow-reconfigure       Provide a writable 'hotconfig' handler.\n\
  -h, --handler ELEMENT.H       Call ELEMENT's read handler H after running\n\
                                driver and print result to standard output.\n\
  -x, --exit-handler ELEMENT.H  Use handler ELEMENT.H value for exit status.\n\
  -o, --output FILE             Write flat configuration to FILE.\n\
  -q, --quit                    Do not run driver.\n\
  -t, --time                    Print information on how long driver took.\n\
  -w, --no-warnings             Do not print warnings.\n"
#ifdef TIMESTAMP_WARPABLE
"      --simtime                 Run in simulation time.\n\
      --simtick                 Amount of subseconds to add in warp time.\n"
#endif
"  -C, --clickpath PATH          Use PATH for CLICKPATH.\n\
      --help                    Print this message and exit.\n\
  -v, --version                 Print version number and exit.\n\
\n\
Report bugs to https://www.fastclick.dev/.\n");
}

struct click_args_t {
  const char *router_file = 0;
  bool file_is_expr = false;
  const char *output_file = 0;
  bool quit_immediately = false;
  bool report_time = false;
  bool allow_reconfigure = false;
  Vector<String> handlers;
  String exit_handler;
  Vector<char*> dpdk_arg;  
  Vector<String> cs_ports;
  Vector<String> cs_unix_sockets;
  Vector<String> cs_sockets;
  bool warnings = true;
  int click_affinity_offset = -1;
  Clp_Parser *clp;
};

int parse(int argc, char** argv, click_args_t &click_args) {
  ErrorHandler* errh = ErrorHandler::default_handler();

#if HAVE_DPDK
  //DPDK EAL expects the application name as first argument
  click_args.dpdk_arg.push_back(argv[0]);
#endif

  // read command line arguments
  Clp_Parser *clp =
    Clp_NewParser(argc, argv, sizeof(options) / sizeof(options[0]), options);
  program_name = Clp_ProgramName(clp);
  click_args.clp = clp;
  while (1) {
    int opt = Clp_Next(clp);
    switch (opt) {

     case ROUTER_OPT:
     case EXPRESSION_OPT:
     router_file:
      if (click_args.router_file) {
        errh->error("router configuration specified twice");
        goto bad_option;
      }
      click_args.router_file = clp->vstr;
      click_args.file_is_expr = (opt == EXPRESSION_OPT);
      break;

     case Clp_NotOption:
      for (const char *s = clp->vstr; *s; s++)
          if (*s == '=' && s > clp->vstr) {
              if (!click_lexer()->global_scope().define(String(clp->vstr, s), s + 1, false))
                  errh->error("parameter %<%.*s%> multiply defined", s - clp->vstr, clp->vstr);
              goto next_argument;
          } else if (!isalnum((unsigned char) *s) && *s != '_')
              break;
      goto router_file;

     case OUTPUT_OPT:
      if (click_args.output_file) {
        errh->error("output file specified twice");
        goto bad_option;
      }
      click_args.output_file = clp->vstr;
      break;

     case HANDLER_OPT:
      click_args.handlers.push_back(clp->vstr);
      break;

     case EXIT_HANDLER_OPT:
      if (click_args.exit_handler) {
        errh->error("--exit-handler specified twice");
        goto bad_option;
      }
      click_args.exit_handler = clp->vstr;
      break;

  case PORT_OPT: {
      uint16_t portno;
      int portno_int = -1;
      String vstr(clp->vstr);
      if (IPPortArg(IP_PROTO_TCP).parse(vstr, portno))
          click_args.cs_ports.push_back(String(portno));
      else if (vstr && vstr.back() == '+'
               && IntArg().parse(vstr.substring(0, -1), portno_int)
               && portno_int > 0 && portno_int < 65536)
          click_args.cs_ports.push_back(String(portno_int) + "+");
      else {
          Clp_OptionError(clp, "%<%O%> expects a TCP port number, not %<%s%>", clp->vstr);
          goto bad_option;
      }
      break;
  }

    case UNIX_SOCKET_OPT:
        click_args.cs_unix_sockets.push_back(clp->vstr);
        break;

    case SOCKET_OPT:
        click_args.cs_sockets.push_back(clp->vstr);
        break;
    case ALLOW_RECONFIG_OPT:
      click_args.allow_reconfigure = !clp->negated;
      break;

    case QUIT_OPT:
      click_args.quit_immediately = true;
      break;

    case TIME_OPT:
      click_args.report_time = true;
      break;

    case WARNINGS_OPT:
      click_args.warnings = !clp->negated;
      break;

    case NO_WARNINGS_OPT:
      click_args.warnings = clp->negated;
      break;
#if HAVE_DPDK
    case DPDK_OPT: {
      const char* arg;
      do {
        arg = Clp_Shift(clp, 1);
        if (arg == NULL) break;
            click_args.dpdk_arg.push_back(const_cast<char*>(arg));
      } while (strcmp(arg, "--") != 0);
      dpdk_enabled = true;
      break;
     }
#endif // HAVE_DPDK
     case THREADS_OPT:
      click_nthreads = clp->val.i;
      if (click_nthreads <= 1)
          click_nthreads = 1;
#if !HAVE_MULTITHREAD
      if (click_nthreads > 1) {
          errh->warning("Click was built without multithread support, running single threaded");
          click_nthreads = 1;
      }
#endif
      break;

     case THREADS_AFF_OPT:
#if HAVE_DECL_PTHREAD_SETAFFINITY_NP
      if (clp->negated)
          click_args.click_affinity_offset = -1;
      else if (clp->have_val)
          click_args.click_affinity_offset = clp->val.i;
      else
          click_args.click_affinity_offset = 0;
#else
      errh->warning("CPU affinity is not supported on this platform");
#endif
      break;

#ifdef TIMESTAMP_WARPABLE
    case SIMTIME_OPT: {
        Timestamp::warp_set_class(Timestamp::warp_simulation);
        Timestamp simbegin(clp->have_val ? clp->val.d : 1000000000);
        Timestamp::warp_set_now(simbegin, simbegin);
        break;
    }
    case SIMTICK_OPT: {
        Timestamp::set_warp_tick(clp->val.ul);
        break;
    }
#endif
     case CLICKPATH_OPT:
      set_clickpath(clp->vstr);
      break;

     case HELP_OPT:
      usage();
      return cleanup(clp, 0);

     case VERSION_OPT:
      printf("click (FastClick) %s\n", CLICK_VERSION);
      printf("Copyright (C) 1999-2001 Massachusetts Institute of Technology\n\
Copyright (C) 2001-2003 International Computer Science Institute\n\
Copyright (C) 2008-2009 Meraki, Inc.\n\
Copyright (C) 2004-2011 Regents of the University of California\n\
Copyright (C) 1999-2012 Eddie Kohler\n\
Copyright (C) 2015-2021 Tom Barbette\n\
This is free software; see the source for copying conditions.\n\
There is NO warranty, not even for merchantability or fitness for a\n\
particular purpose.\n");
      return cleanup(clp, 0);

     bad_option:
     case Clp_BadOption:
      short_usage();
      return cleanup(clp, 1);

     case Clp_Done:
      goto done;

    }
   next_argument: ;
  }
done:
  return 0;
}
