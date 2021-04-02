#ifndef LIBNICSCHEDULER_SOLVER_HH
#define LIBNICSCHEDULER_SOLVER_HH
#include <float.h>
#include <queue>
#include <vector>
#include <limits.h>
#include <algorithm>

/*
 * Structure to keep a load and an id
 * Better than anonymous pairs. Used for CPUs, buckets, ...
 */
struct cref {
    float load;
    int id;
};

/**
 * Comparator for cref
 */
class Compare
{
public:
    bool operator() (cref a, cref b)
    {
        return a.load < b.load;
    }
};

/**
 * Take some buckets of a list to make the load of a set of CPU
 *  reach target, this is the main RSS++ algorithm as
 *  described in the paper
 */
class BucketMapTargetProblem
{ public:
    std::vector<int> transfer; //existing core id for each buckets
    std::vector<float> target; //Target for destination
    std::vector<float> max; //Max load to take from overloaded
    std::vector<int> buckets_max_idx; //Load for each bucket
    std::vector<float> buckets_load; //Load for each bucket
    float square_imbalance = 0; //Final load imbalance (after calling solve)

    BucketMapTargetProblem(int nbuckets, int nunderloaded, int noverloaded ) : min_cost(FLT_MAX) {
        transfer.resize(nbuckets,-1);
        buckets_load.resize(nbuckets);
        buckets_max_idx.resize(nbuckets);
        target.resize(nunderloaded);
        max.resize(noverloaded);
    }

    void solve(NICScheduler* balancer) {
        click_chatter("Assigning %lu buckets to %lu targets :", buckets_load.size(), target.size());

        if (unlikely(balancer->verbose() > 1)) {
            for (int i = 0; i < max.size(); i++) {
                click_chatter("Overloaded %d , %f",i, max[i]);
            }
            for (int i = 0; i < target.size(); i++) {
                click_chatter("Underloaded %d , %f",i, target[i]);
            }
            for (int i = 0; i < buckets_load.size(); i++) {
                click_chatter("Bucket %d , %f, oid %d",i, buckets_load[i], buckets_max_idx[i]);
            }
        }

        float target_imbalance = 0.01;//(o_load - u_loadà ;
        //Build priority for each overloaded
        //

        auto cmplt = [](cref left, cref right) { return left.load > right.load; };
        auto cmpgt = [](cref left, cref right) { return left.load < right.load; };

        float overload_allowed =  -0.01; //Take just not enough load of overloaded
        float underload_allowed = -0.01; //Give just not enough to underloaded
        float imbalance_u = 0;
        float imbalance_o = 0;
        float oa_min;
        float ua_min;
        float bottom_oa = overload_allowed;
        float top_oa;
        float bottom_ua = underload_allowed;
        float top_ua;

        float last_sq = FLT_MAX;
        float min_sq = FLT_MAX;
        float bottom_sq = FLT_MAX;
        float top_sq = FLT_MAX;
        int run = 1;
        float m = -1;
        int phase;
#define max_runs 10

        while(true) {

            Timestamp run_begin = Timestamp::now_steady();
            imbalance_u = 0;
            imbalance_o = 0;
            square_imbalance = 0;
            typedef std::priority_queue<cref, std::vector<cref>, Compare> bstack;
            std::vector<bstack> bstacks;
            bstacks.resize(max.size(),bstack());
            for (int i = 0; i < buckets_load.size(); i++) {
                bstacks[buckets_max_idx[i]].push(cref{buckets_load[i],i});
                transfer[i] = -1;
            }

            std::priority_queue<cref, std::vector<cref>, decltype(cmpgt)> overloaded(cmpgt);
            for (int i = 0; i < max.size(); i++) {
                overloaded.push(cref{max[i],i});
            }

            std::priority_queue<cref, std::vector<cref>, decltype(cmpgt)> underloaded(cmpgt);
            for (int i = 0; i < target.size(); i++) {
                underloaded.push(cref{target[i],i});
            }


            while (!overloaded.empty() && !underloaded.empty()) {
                next_core:
                //Select most overloaded core
                cref o = overloaded.top();
                overloaded.pop(); //Will be added bacj

                //Select biggest bucket
                bstack& buckets = bstacks[o.id];
                cref bucket = buckets.top();
                buckets.pop();//Bucket is removed forever

                //Assign its most used buckets
                std::vector<cref> save;
                while (!underloaded.empty()) {
                    cref u = underloaded.top();
                    underloaded.pop();

                    if (unlikely(balancer->verbose() > 2))
                        click_chatter("U%d load %f",u.id, u.load);
                    if (bucket.load < u.load + underload_allowed) {
                        u.load -= bucket.load;
                        o.load -= bucket.load;
                        transfer[bucket.id] = u.id;

                        if (unlikely(balancer->verbose() > 1))
                            click_chatter("Bucket %d to ucore %d, bucket load %f", bucket.id, u.id, bucket.load);
                        if (u.load > target_imbalance) {
                            underloaded.push(u);
                        } else {
                            imbalance_u += abs(u.load);
                            square_imbalance += u.load * u.load;
                            if (unlikely(balancer->verbose() > 1))
                                click_chatter("Underloaded core %d is now okay with %f load", u.id, u.load);
                        }
                        goto bucket_assigned;
                    } else {
                        save.push_back(u);
                    }
                }

                if (unlikely(balancer->verbose() > 2))
                    click_chatter("Bucket %d UNMOVED, load %f", bucket.id, bucket.load);

                bucket_assigned:
                while (!save.empty()) {
                    underloaded.push(save.back());
                    save.pop_back();
                }

                if (o.load > - overload_allowed
                        && !buckets.empty()) {
                    overloaded.push(o);
                } else {
                    imbalance_o += abs(o.load);
                    square_imbalance += o.load * o.load;
                    if (unlikely(balancer->verbose() > 1))
                        click_chatter("Overloaded core %d is now okay with %f load. Empty : %d", o.id, o.load,buckets.empty());
                }
            }
            while (!overloaded.empty()) {
                auto o = overloaded.top();
                imbalance_o += abs(o.load);
                square_imbalance += o.load * o.load;
                overloaded.pop();
            }
            while (!underloaded.empty()) {
                auto u = underloaded.top();
                imbalance_u += abs(u.load);
                square_imbalance += u.load * u.load;
                underloaded.pop();
            }


            Timestamp run_end = Timestamp::now_steady();
            unsigned time = (run_end - run_begin).usecval();
            if (unlikely(balancer->verbose()))
                click_chatter("Imbalance at run %d : %f-%f %f-%f, square %f, m %f, in %d usec",run,imbalance_o,imbalance_u,overload_allowed,underload_allowed,square_imbalance, m, time);

            NICScheduler::RunStat &v = balancer->stats(run - 1);
            v.imbalance += square_imbalance;
            v.count ++;
            v.time += time;

            if (run == max_runs || square_imbalance < target_imbalance) break;

            if (run == 1) {
                overload_allowed = imbalance_o;
                underload_allowed = imbalance_u;
                phase = 1; //searching top
            } else {

                if (square_imbalance < min_sq) {
                    oa_min = overload_allowed;
                    ua_min = underload_allowed;
                }

                if (phase == 1) {
                    if (square_imbalance <= last_sq) { //Continue finding top
                        overload_allowed = overload_allowed + overload_allowed / 2;
                        underload_allowed = underload_allowed + underload_allowed / 2;
                    } else {
                        phase = 2;
                        top_oa = overload_allowed;
                        top_ua = underload_allowed;
                        m = 0.5;
                        overload_allowed = overload_allowed / 2;
                        underload_allowed = underload_allowed / 2;
                    }
                } else if (phase == 2) { //Searching left inflation
                    if (square_imbalance <= last_sq) {
                        m = (0 + m) / 2; //continue left;
                        if (m < 0.01)
                            break; //Border is the max
                        overload_allowed = bottom_oa + (top_oa - bottom_oa) * m;
                        underload_allowed = bottom_ua + (top_ua - bottom_ua) * m;
                        //Either we still need to descend left
                        //Or we hit the left inflation and will need to descend right afterwards
                    } else if (square_imbalance > last_sq) { //we found a new bottom
                        bottom_oa = overload_allowed;
                        bottom_ua = underload_allowed;
                        bottom_sq = square_imbalance;
                        phase = 3;
                        m = 0.5;
                        overload_allowed = (bottom_oa + top_oa) / 2;
                        underload_allowed = (bottom_ua + top_ua) / 2;
                    }
                } else if (phase == 3) { //Searching right inflation
                    if (square_imbalance <= last_sq) {
                        m = (1 + m) / 2; // continue right
                        if (m > 0.99)
                            break; //Border is the min
                        overload_allowed = bottom_oa + (top_oa - bottom_oa) * m;
                        underload_allowed = bottom_ua + (top_ua - bottom_ua) * m;
                    } else     if (square_imbalance > last_sq) { //we found a new top
                        phase = 2;
                        top_oa = overload_allowed;
                        top_ua = underload_allowed;
                        top_sq = square_imbalance;
                        m = 0.5;
                        overload_allowed = bottom_oa + (top_oa - bottom_oa) * m;
                        underload_allowed = bottom_ua + (top_ua - bottom_ua) * m;
                    }
                }

                if (top_sq == bottom_sq && bottom_sq == square_imbalance && square_imbalance == min_sq) {
                    break;
                }

            }
            if (unlikely(balancer->verbose() > 2))
                click_chatter("Phase %d", phase);

            run++;
            last_sq = square_imbalance;
            if (run == max_runs) {
                if (square_imbalance != min_sq) {
                    overload_allowed = oa_min;
                    underload_allowed = ua_min;
                } else
                    break;
            }
        }
    }

private:
    float min_cost;
};

/**
 * Problem of moving all given buckets to a set of cores, ensuring load balancing.
 *
 * It is solved by simply pushing load of each bucket one by one to the core
 * with the least load. We start with the most loaded buckets first.
 * Not the most optimal solution, but we do not care as we will rebalance
 * shortly.
 */
class BucketMapProblem
{ public:
    std::vector<int> transfer; //existing core id for each buckets
    std::vector<float> imbalance; //Imbalance for each existing cores (no holes)
    std::vector<float> buckets_load; //Load for each bucket

    BucketMapProblem(int nbuckets, int ncpu) : min_cost(FLT_MAX) {
        transfer.resize(nbuckets);
        buckets_load.resize(nbuckets);
        imbalance.resize(ncpu);
    }

    float solve() {
        typedef struct {
            int id;
            float load;
        } bref;
        auto cmp = [](bref left, bref right) { return left.load > right.load; };
        std::priority_queue<bref, std::vector<bref>, decltype(cmp)> q(cmp);
        for(int i = 0; i < buckets_load.size(); i++) {
            float f = buckets_load[i];

            q.push(bref{.id = i,.load =   f});
        }

        auto cmpc = [](bref left, bref right) { return left.load < right.load; };
        std::priority_queue<bref, std::vector<bref>, decltype(cmp)> cores(cmp);
        for(int i = 0; i < imbalance.size(); i++) {
            float f = imbalance[i];
            //click_chatter("Core %d should receive %f load",i,f);
            cores.push(bref{.id = i,.load = - f});//negative of imbalance, so we should reach a nice 0 everywhere by adding some load
        }


        while (!q.empty()) {
            bref t = q.top();
            q.pop();
            bref c = cores.top();
            cores.pop();
            transfer[t.id] = c.id;
            c.load += t.load;
            cores.push(c);
        }

        float total_imbalance = 0;
        for (int i = 0; i < cores.size(); i++) {
            total_imbalance += abs(cores.top().load);
            cores.pop();
        }
        return total_imbalance;
    }

private:
    float min_cost;
};





#endif
