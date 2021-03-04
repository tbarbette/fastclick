#ifndef LIBNICSCHEDULER_METHODRSSPP_HH
#define LIBNICSCHEDULER_METHODRSSPP_HH 1

class MethodRSSPP : public MethodRSS, public LoadTracker { public:

    MethodRSSPP(NICScheduler* b, EthernetDevice* fd);

    int initialize(ErrorHandler *errh, int startwith) override;

    virtual std::string name() override CLICK_COLD { return "rsspp"; }

    void rebalance(std::vector<std::pair<int,float>> load) override;

    //We keep the space here to avoid reallocation
    struct Node {
        uint64_t count;
        uint64_t variance;
        bool moved;
    };
    std::vector<Node> _count;

#ifndef HAVE_DPDK
#define _counter_is_xdp false
#else
    bool _counter_is_xdp;
#endif
    int _xdp_table_fd;
    void* _counter;

    float _target_load;
    float _imbalance_alpha;
    float _threshold;

    bool _dancer;
    bool _numa;
    int _numa_num;

  private:
    /**
     * Class used to keep information about a core ad its load according to various metrics
     */
    class Load { public:

        Load() : Load(-1) {

        }

        Load(int phys_id) : cpu_phys_id(phys_id), load(0), high(false), npackets(0), nbuckets(0), nbuckets_nz(0)  {

        }
        int cpu_phys_id;
        float load;
        bool high;
        unsigned long long npackets;
        unsigned nbuckets;
        unsigned nbuckets_nz;
    };

    /* @brief apply a list of moves */
    void apply_moves(std::function<int(int)> cpumap, std::vector<std::vector<std::pair<int,int>>> omoves, const Timestamp &begin);


};

#endif
