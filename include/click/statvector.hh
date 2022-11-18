// -*- c-basic-offset: 4 -*-
#ifndef CLICK_STATVECTOR_HH
#define CLICK_STATVECTOR_HH
#include <click/batchelement.hh>
#include <click/multithread.hh>
#include <click/vector.hh>
#include <click/straccum.hh>
#include <click/statvector.hh>
CLICK_DECLS


template <typename T>
class StatVector {



    enum{H_MEDIAN,H_AVERAGE,H_DUMP,H_MAX_OBS,H_N_OBS,H_NZ,H_MAX,H_MAX_OBS_VAL};

    static String read_handler(Element *e, void *thunk)
    {
        StatVector *fd = (StatVector*)e->cast("StatVector");
        switch ((intptr_t)thunk) {
        case H_MAX:
        case H_NZ:
        case H_MAX_OBS: {
            Vector<T> sums(fd->stats.get_value(0).size(),0);
            T max_batch_v = -1;
            int max_batch_index = -1;
            int max_nz = -1;
            int nz=0;

            for (unsigned j = 0; j < (unsigned)sums.size(); j++) {
                for (unsigned i = 0; i < fd->stats.weight(); i++) {
                    sums[j] += fd->stats.get_value(i)[j];
                }
                if (sums[j] > max_batch_v) {
                    max_batch_v = sums[j];
                    max_batch_index = j;
                }
                if (sums[j] > 0) {
                    max_nz = j;
                    nz++;
                }
            }
            if ((intptr_t)thunk == H_MAX) {
                return String(max_nz);
            } else if ((intptr_t)thunk == H_NZ) {
                return String(nz);
            } else
                return String(max_batch_index);
        }
        case H_N_OBS:
        case H_MEDIAN: {
            Vector<T> sums(fd->stats.get_value(0).size(),0);
            T total = 0;
            for (unsigned j = 0; j < (unsigned)sums.size(); j++) {
                for (unsigned i = 0; i < fd->stats.weight(); i++) {
                    sums[j] += fd->stats.get_value(i)[j];
                }
                total += sums[j];
            }
            if ((intptr_t)thunk == H_N_OBS)
                return String(total);
            T val = 0;
            for (int i = 0; i < sums.size(); i++) {
                val += sums[i];
                if (val > total/2)
                    return String(i);
            }
            return "0";
        }
        case H_AVERAGE: {
            int count = 0;
            int total = 0;
            for (unsigned i = 0; i < fd->stats.weight(); i++) {
                for (unsigned j = 0; j < (unsigned)fd->stats.get_value(i).size(); j++) {
                    total += fd->stats.get_value(i)[j] * j;
                    count += fd->stats.get_value(i)[j];
                }
            }
            if (count > 0)
                return String((double)total/(double)count);
            else
                return String(0);
        }
        case H_DUMP: {
            StringAccum s;
            Vector<T> sums(fd->stats.get_value(0).size(),0);
            for (unsigned i = 0; i < fd->stats.weight(); i++) {
                for (unsigned j = 0; j < (unsigned)fd->stats.get_value(i).size(); j++) {
                    sums[j] += fd->stats.get_value(i)[j];
                    if (i == fd->stats.weight() - 1 && sums[j] != 0)
                        s << j << ": " << sums[j] << "\n";
                }
            }
            return s.take_string();
        }
        default:
        return "<error>";
        }
    }

protected:
    per_thread<Vector<T>> stats;

    StatVector() {

    }

    StatVector(Vector<T> v) : stats(v) {

    }
    void add_stat_handler(Element* e) {
        //Value the most seen (gives the value)
        e->add_read_handler("most_seen", read_handler, H_MAX_OBS, Handler::f_expensive);
        //Value the most seen (gives the frequency of the value)
        e->add_read_handler("most_seen_freq", read_handler, H_MAX_OBS_VAL, Handler::f_expensive);
        //Maximum value seen
        e->add_read_handler("max", read_handler, H_MAX, Handler::f_expensive);
        //Number of observations
        e->add_read_handler("count", read_handler, H_N_OBS, Handler::f_expensive);
        //Number of values that had at least one observations
        e->add_read_handler("nval", read_handler, H_NZ, Handler::f_expensive);
        //Value for the median number of observations
        e->add_read_handler("median", read_handler, H_MEDIAN, Handler::f_expensive);
        //Average of value*frequency
        e->add_read_handler("average", read_handler, H_AVERAGE, Handler::f_expensive);
        e->add_read_handler("avg", read_handler, H_AVERAGE, Handler::f_expensive);
        //Dump all value: frequency
        e->add_read_handler("dump", read_handler, H_DUMP, Handler::f_expensive);
    }
};

CLICK_ENDDECLS
#endif //CLICK_STATVECTOR_HH
