#include <click/config.h>
#include <click/clp.h>
#include <rte_ethdev.h>
#include <stdlib.h>
#include <click/pathvars.h>

static int
cleanup(Clp_Parser *clp, int exit_value)
{
    Clp_DeleteParser(clp);
    return exit_value;
}

#include "args.hh"

#define exec system

#define perfalert(args...) click_chatter("PERFORMANCE WARNING:" args "!")

int main(int argc, char** argv) {
#if !HAVE_DPDK
    printf("You need DPDK to use PacketMill!\n");
    return 1;
#endif
    click_args_t args;
    char cmd[256];
    parse(argc, argv, args);  
   
    /*int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");*/

#if !HAVE_DPDK_XCHG
    perfalert("PacketMill was NOT compiled with XCHG");
#endif

    chdir(CLICK_DIR "/userlevel/" );
    sprintf(cmd, "../bin/click-devirtualize --inline --static %s > package.uo", args.router_file);
    exec(cmd);
    exec("ar x package.uo config");
    exec("../bin/click-mkmindriver -V -C $(pwd)/../ -p embed --ship -u");
    exec("make embedclick MINDRIVER=embed STATIC=1");
    exec("cat config | tail -n +2 > config_stripped");
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i],args.router_file) == 0)
            argv[i] = "config_stripped";
    }
    argv[0] = "./embedclick";
    execvp("./embedclick",argv);
    return 0;
}
