/*
 * PacketMill
 *
 * Copyright (c) 2021, Tom Barbette, KTH Royal Institute of Technology - All Rights Reserved 
 * Copyright (c) 2021, Alireza Farshin, KTH Royal Institute of Technology - All Rights Reserved
 *
 */

#include <click/clp.h>
#include <click/config.h>
#if HAVE_DPDK
#include <rte_ethdev.h>
#endif
#if HAVE_LLVM
#include <click/llvmutils.hh>
#endif
#include <click/pathvars.h>
#include <stdlib.h>

static int cleanup(Clp_Parser *clp, int exit_value) {
  Clp_DeleteParser(clp);
  return exit_value;
}

#include "args.hh"

#define exec system

#define perfalert(args...) click_chatter("PERFORMANCE WARNING:" args "!")

int main(int argc, char **argv) {
#if !HAVE_DPDK
  printf("You need DPDK to use PacketMill!\n");
  return 1;
#endif
  click_args_t args;
  char cmd[512];
  parse(argc, argv, args);
#if HAVE_DPDK
  /*int ret = rte_eal_init(argc, argv);
  if (ret < 0)
      rte_panic("Cannot init EAL\n");*/
#endif
#if !HAVE_DPDK_XCHG
  perfalert("PacketMill was NOT compiled with XCHG");
#endif

  char pwd[256];

  char cpath[256];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"

  getcwd(pwd, 256);
  chdir(CLICK_DIR "/userlevel/");
  snprintf(cpath, 256, "%.*s/%.*s", 127,args.router_file[0] == '/' ? "." : pwd,
          127,args.router_file);
  sprintf(
      cmd,
      "../bin/click-devirtualize --inline --replace --static %s > package.uo",
      cpath);
  exec(cmd);
  sprintf(cmd, "ar x package.uo config");
  exec(cmd);
  sprintf(cmd, "../bin/click-mkmindriver -V -C $(pwd)/../ -p embed --ship -u "
               "package.uo");
  exec(cmd);
  exec("make embedclick MINDRIVER=embed STATIC=1");
  #if HAVE_LLVM
    if (optimizeIR("embedclick")) {
      click_chatter("Applying -O3 optimizations");
      exec("opt -S -O3 embedclick.ll -o embedclick.ll");
      exec("make embedclick-opt");
      argv[0] = "./embedclick-opt";
    } else {
      argv[0] = "./embedclick";
    }
  #else
    perfalert("Skipping IR optimizations, as LLVM libraries could not be found!");
    argv[0] = (char*)"./embedclick";
  #endif
  exec("cat config | tail -n +2 > config_stripped");
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], args.router_file) == 0)
      argv[i] = (char*)"config_stripped";
  }
  /* Hint to NPF Packet Generator */
  click_chatter("EVENT COMPILED");
  execvp(argv[0], argv);
#pragma GCC diagnostic pop
  return 0;
}
