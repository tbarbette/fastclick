#include <click/config.h>
#include <click/clp.h>
#if HAVE_DPDK
#include <rte_ethdev.h>
#endif
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
        getcwd(pwd, 256);
    chdir(CLICK_DIR "/userlevel/" );
    sprintf(cpath,"%s/%s", args.router_file[0] == '/' ? "." : pwd, args.router_file);
    sprintf(cmd, "../bin/click-devirtualize --inline --replace --static %s > package.uo", cpath );
    exec(cmd);
    sprintf(cmd, "ar x package.uo config");
    exec(cmd);
    sprintf(cmd, "../bin/click-mkmindriver -V -C $(pwd)/../ -p embed --ship -u package.uo");
    exec(cmd);
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
