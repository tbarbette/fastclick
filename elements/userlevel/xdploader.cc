#include <net/if.h>
#include <linux/if.h>
#include <click/config.h>
#include <click/args.hh>

#include "xdploader.hh"


XDPLoader::XDPLoader() : _path(), _dev() {
}

XDPLoader::~XDPLoader() {
}

int
XDPLoader::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(this, errh).bind(conf)
        .read_mp("PATH", _path)
        .read_mp("DEV", _dev)
        .read_or_set("CLEAN", _do_clean, true)
        .consume() < 0)
        return -1;

    return 0;
}


Vector<unsigned>
XDPLoader::read_per_cpu_array(int map_fd, int size, int cpus) {
	Vector<unsigned> map;
	map.resize(size,0);
    for (__u32 key = 0; key < size; key++) {
        __u64 entry[64];
        if (!bpf_map_lookup_elem(map_fd, &key, entry)) {
            __u64 tot = 0;
            for (int i = 0; i < cpus; i++) {
                tot += entry[i];
            }
            map[key]  = tot;
        }
    }
    return map;
}

int
XDPLoader::get_map_fd(String name) {
	struct bpf_map *map;
	int map_fd;
	map = bpf_object__find_map_by_name(_obj, name.c_str());
	if (!map)
		error(1, errno, "can't load drop_map");
	map_fd = bpf_map__fd(map);

	if (map_fd < 0)
		error(1, errno, "can't get map %s fd", name.c_str());

	return map_fd;
}

int
XDPLoader::initialize(ErrorHandler *errh) {
    int prog_fd;

    _obj = bpf_object__open_file(_path.c_str(), NULL);
    if (libbpf_get_error(_obj))
        error(1, errno, "can't open %s", _path.c_str());

    // Load all the programs as XDP programs and use the first one
    struct bpf_program *prog;
    bpf_object__for_each_program(prog, _obj)
        bpf_program__set_type(prog, BPF_PROG_TYPE_XDP);

    if (bpf_object__load(_obj))
        error(1, errno, "can't load %s", _path.c_str());

    prog = bpf_object__next_program(_obj, NULL);
    if (!prog)
        error(1, 0, "no program in %s", _path.c_str());
    prog_fd = bpf_program__fd(prog);

    click_chatter("Loaded!");

    _ifindex = if_nametoindex(_dev.c_str());
    if (!_ifindex)
        error(1, errno, "unknown interface %s\n", _dev.c_str());

    if (bpf_xdp_attach(_ifindex, prog_fd, 0, NULL) < 0)
        printf("can't attach to interface %s:%d: %d:%s\n", _dev.c_str(), _ifindex, errno,
              strerror(errno));

    return 0;
}

void
XDPLoader::cleanup(CleanupStage stage) {
    // cleaning-up
    if (_do_clean)
        bpf_xdp_detach(_ifindex, 0, NULL);
}

ELEMENT_REQUIRES(bpf)
EXPORT_ELEMENT(XDPLoader)
