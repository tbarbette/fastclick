// -*- c-basic-offset: 4 -*-
#ifndef CLICK_NUMA_HH
#define CLICK_NUMA_HH

#include <click/bitvector.hh>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <numa.h>
}


class NumaCpuBitmask {
	NumaCpuBitmask() {
		numa_available();
		b = numa_allocate_cpumask();
	};

public:
    ~NumaCpuBitmask() {
		numa_free_cpumask(b);
	}

	struct bitmask* bitmask() {
		return b;
	}

	bool isBitSet(unsigned int n) {
		return numa_bitmask_isbitset(b,n);
	}


	static NumaCpuBitmask allocate() {
		numa_available();
		NumaCpuBitmask bcp;
		return bcp;
	}

	NumaCpuBitmask& operator=(const NumaCpuBitmask& other) {
		copy_bitmask_to_bitmask(other.b,b);
		return *this;
	}

	NumaCpuBitmask& operator|=(const NumaCpuBitmask& bmp) {
		bmp.print();
		for (int i = 0; i < numa_num_configured_cpus(); i++)
			if (bmp.get(i))
				set(i);
		return *this;
	}

	NumaCpuBitmask(const NumaCpuBitmask& other ) {
		NumaCpuBitmask bcp = NumaCpuBitmask::allocate();
		copy_bitmask_to_bitmask(other.b,bcp.b);
	}


	void set(int i) {
		numa_bitmask_setbit(b,i);
	}

	bool get(int i) const {
		return numa_bitmask_isbitset(b, i);
	}

	void print() const {
		char s[numa_num_configured_cpus() + 1];
		for (int i = 0; i < numa_num_configured_cpus(); i++) {
			if (get(i))
				s[i] = '1';
			else
				s[i] = '0';
		}
		s[numa_num_configured_cpus()] = '\0';
		click_chatter("%d : %s",numa_num_configured_cpus(),s);
	}

	void toBitvector(Bitvector &b) {
		if (numa_num_configured_cpus() > b.size())
			b.resize(numa_num_configured_cpus());
		for (int i = 0; i < numa_num_configured_cpus(); i++) {
			if (get(i))
				b[i] = 1;
		}

	}

private:
	struct bitmask* b;

};

class Numa {
public:


	static NumaCpuBitmask all_cpu() {
		NumaCpuBitmask bcp = NumaCpuBitmask::allocate();
		copy_bitmask_to_bitmask(numa_all_cpus_ptr,bcp.bitmask());
		return bcp;
	}

	static NumaCpuBitmask node_to_cpus(int node) {
		NumaCpuBitmask bcp = NumaCpuBitmask::allocate();
		numa_node_to_cpus(node,bcp.bitmask());
		return bcp;
	}

	static int get_max_cpus() {
		return numa_num_configured_cpus();
	}

	static int get_max_numas() {
		return numa_num_configured_nodes();
	}

	static int get_numa_node_of_cpu(int cpuid) {
		return numa_node_of_cpu(cpuid);
	}

	static int get_device_node(const char* device) {

		char path[100];
		sprintf(path, "/sys/class/net/%s/device/numa_node", device);
		int fd = open(path, O_RDONLY);
		int i = read(fd, path,100);
		if (i <= 0)
			return 0;

		path[i] = '\0';
		sscanf(path,"%d",&i);
		close(fd);
		if (i == -1)
			return 0;
		return i;
	}

	static void print(Bitvector& b) {
		char s[numa_num_configured_cpus() + 1];
		for (int i = 0; i < numa_num_configured_cpus(); i++) {
			if (b[i])
				s[i] = '1';
			else
				s[i] = '0';
		}
		s[numa_num_configured_cpus()] = '\0';
		click_chatter("%d : %s",numa_num_configured_cpus(),s);
	}
};

#endif
