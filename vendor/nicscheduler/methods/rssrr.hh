
class MethodRSSRR : public MethodRSS { public:

    MethodRSSRR(NICScheduler* b, EthernetDevice* fd);

    ~MethodRSSRR();

    virtual std::string name() override CLICK_COLD { return "rssrr"; }

    void rebalance(std::vector<std::pair<int,float>> load) override;
};
