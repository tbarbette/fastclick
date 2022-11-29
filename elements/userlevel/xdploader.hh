#ifndef CLICK_XDPLOADER_HH
#define CLICK_XDPLOADER_HH

#include <click/element.hh>
#include <click/hashtable.hh>
#include <click/string.hh>
#include <click/vector.hh>
#include <click/bpf.hh>

extern "C" {
#include <error.h>
#include <errno.h>
}

CLICK_DECLS


/*
=c

XDPLoader

=s 

Load a BPF program at PATH into the XDP device DEV

*/

class XDPLoader : public Element {
public:

	XDPLoader() CLICK_COLD;
    ~XDPLoader() CLICK_COLD;

    const char *class_name() const { return "XDPLoader"; }
    const char *port_count() const { return "0/0"; }
    const char *processing() const { return AGNOSTIC; }
    int configure_phase()    const { return CONFIGURE_PHASE_FIRST; }

    bool can_live_reconfigure() const { return false; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;
    int initialize(ErrorHandler *) override CLICK_COLD;

    void cleanup(CleanupStage stage) override CLICK_COLD;

    Vector<unsigned> read_per_cpu_array(int table_fd, int size, int cpus);

    int get_map_fd(String name);

private:
    String _path;
    String _dev;
    int _ifindex;
    struct bpf_object* _obj;
    bool _do_clean;
};

static inline unsigned int bpf_num_possible_cpus(void)
{
        static const char *fcpu = "/sys/devices/system/cpu/possible";
        unsigned int start, end, possible_cpus = 0;
        char buff[128];
        FILE *fp;
        int len, n, i, j = 0;

        fp = fopen(fcpu, "r");
        if (!fp) {
                printf("Failed to open %s: '%s'!\n", fcpu, strerror(errno));
                exit(1);
        }

        if (!fgets(buff, sizeof(buff), fp)) {
                printf("Failed to read %s!\n", fcpu);
                exit(1);
        }

        len = strlen(buff);
        for (i = 0; i <= len; i++) {
                if (buff[i] == ',' || buff[i] == '\0') {
                        buff[i] = '\0';
                        n = sscanf(&buff[j], "%u-%u", &start, &end);
                        if (n <= 0) {
                                printf("Failed to retrieve # possible CPUs!\n");
                                exit(1);
                        } else if (n == 1) {
                                end = start;
                        }
                        possible_cpus += end - start + 1;
                        j = i + 1;
                }
        }

        fclose(fp);


        return possible_cpus;
}



/*
template <typename T> void
XDPLoader::read_per_cpu_array_count(int map_fd, int cpus, Vector<T>& table) {
    for (__u32 key = 0; key < table.size(); key++) {
        __u64 entry[64];
        if (!bpf_map_lookup_elem(map_fd, &key, entry)) {
            __u64 tot = 0;
            for (int i = 0; i < cpus; i++) {
                tot += entry[i];
            }
            table[key].variance  = (table[key].variance  / 3) + (2 * table[key].count));
            table[key].count  = tot;

        }
    }
}*/
#ifndef __u32
#define __u32 uint32_t
#define __u64 uint32_t
#endif

CLICK_ENDDECLS
#endif
