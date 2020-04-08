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



    enum{H_MEDIAN,H_AVERAGE,H_DUMP};

    static String read_handler(Element *e, void *thunk)
    {
        StatVector *fd = (StatVector*)e->cast("StatVector");
        switch ((intptr_t)thunk) {
        case H_MEDIAN: {
            Vector<int> sums(fd->stats.get_value(0).size(),0);
            int max_batch_v = -1;
            int max_batch_index = -1;
            for (unsigned i = 0; i < fd->stats.weight(); i++) {
                for (unsigned j = 0; j < fd->stats.get_value(i).size(); j++) {
                    sums[j] += fd->stats.get_value(i)[j];
                    if (sums[j] > max_batch_v) {
                        max_batch_v = sums[j];
                        max_batch_index = j;
                    }
                }
            }
            return String(max_batch_index);
        }
        case H_AVERAGE: {
            int count = 0;
            int total = 0;
            for (unsigned i = 0; i < fd->stats.weight(); i++) {
                for (unsigned j = 0; j < fd->stats.get_value(i).size(); j++) {
                    total += fd->stats.get_value(i)[j] * j;
                    count += fd->stats.get_value(i)[j];
                }
            }
            if (count > 0)
            return String(total/count);
            else
            return String(0);
        }
        case H_DUMP: {
            StringAccum s;
            Vector<T> sums(fd->stats.get_value(0).size(),0);
            for (unsigned i = 0; i < fd->stats.weight(); i++) {
                for (unsigned j = 0; j < fd->stats.get_value(i).size(); j++) {
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
        e->add_read_handler("median", read_handler, H_MEDIAN, Handler::f_expensive);
        e->add_read_handler("average", read_handler, H_AVERAGE, Handler::f_expensive);
        e->add_read_handler("avg", read_handler, H_AVERAGE, Handler::f_expensive);
        e->add_read_handler("dump", read_handler, H_DUMP, Handler::f_expensive);
    }
};

CLICK_ENDDECLS
#endif //CLICK_STATVECTOR_HH
