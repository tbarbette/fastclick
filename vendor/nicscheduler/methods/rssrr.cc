
/**
 * RSS - RoundRobin
 */

MethodRSSRR::MethodRSSRR(NICScheduler* b, EthernetDevice* fd) : MethodRSS(b,fd) {
};

MethodRSSRR::~MethodRSSRR() {
};

void MethodRSSRR::rebalance(std::vector<std::pair<int,float>> load) {
    for (int r = 0; r < _table.size(); r++) {
        _table[r] = (_table[r] + 1) % load.size();
    }
    update_reta();
};

