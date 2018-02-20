// -*- c-basic-offset: 4 -*-
/*
 * hardwarecounter.{cc,hh}
 * Tom Barbette
 *
 * Copyright (c) 2017 University of Liege
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "hardwarecounter.hh"
#include <click/string.hh>
#include <click/straccum.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <papi.h>

CLICK_DECLS


static HardwareCounter::event known_events[] = {
        {"L1_DCM", PAPI_L1_DCM,       "Level 1 data cache misses"},
        {"L1_ICM", PAPI_L1_ICM,       "Level 1 instruction cache misses"},
        {"L2_DCM", PAPI_L2_DCM,       "Level 2 data cache misses"},
        {"L2_ICM", PAPI_L2_ICM,       "Level 2 instruction cache misses"},
        {"L3_DCM", PAPI_L3_DCM,       "Level 3 data cache misses"},
        {"L3_ICM", PAPI_L3_ICM,       "Level 3 instruction cache misses"},
        {"L1_TCM", PAPI_L1_TCM,       "Level 1 total cache misses"},
        {"L2_TCM", PAPI_L2_TCM,       "Level 2 total cache misses"},
        {"L3_TCM", PAPI_L3_TCM,       "Level 3 total cache misses"},
        {"CA_SNP", PAPI_CA_SNP,       "Snoops"},
        {"CA_SHR", PAPI_CA_SHR,       "Request for shared cache line (SMP)"},
        {"CA_CLN", PAPI_CA_CLN,       "Request for clean cache line (SMP)"},
        {"CA_INV", PAPI_CA_INV,       "Request for cache line Invalidation (SMP)"},
        {"CA_ITV", PAPI_CA_ITV,       "Request for cache line Intervention (SMP)"},
        {"L3_LDM", PAPI_L3_LDM,       "Level 3 load misses"},
        {"L3_STM", PAPI_L3_STM,       "Level 3 store misses"},
        {"BRU_IDL", PAPI_BRU_IDL,     "Cycles branch units are idle"},
        {"FXU_IDL", PAPI_FXU_IDL,     "Cycles integer units are idle"},
        {"FPU_IDL", PAPI_FPU_IDL,     "Cycles floating point units are idle"},
        {"LSU_IDL", PAPI_LSU_IDL,     "Cycles load/store units are idle"},
        {"TLB_DM", PAPI_TLB_DM,       "Data translation lookaside buffer misses"},
        {"TLB_IM", PAPI_TLB_IM,       "Instr translation lookaside buffer misses"},
        {"TLB_TL", PAPI_TLB_TL,       "Total translation lookaside buffer misses"},
        {"L1_LDM", PAPI_L1_LDM,       "Level 1 load misses"},
        {"L1_STM", PAPI_L1_STM,       "Level 1 store misses"},
        {"L2_LDM", PAPI_L2_LDM,       "Level 2 load misses"},
        {"L2_STM", PAPI_L2_STM,       "Level 2 store misses"},
        {"BTAC_M", PAPI_BTAC_M,       "BTAC miss"},
        {"PRF_DM", PAPI_PRF_DM,       "Prefetch data instruction caused a miss"},
        {"L3_DCH", PAPI_L3_DCH,       "Level 3 Data Cache Hit"},
        {"TLB_SD", PAPI_TLB_SD,       "Xlation lookaside buffer shootdowns (SMP)"},
        {"CSR_FAL", PAPI_CSR_FAL,     "Failed store conditional instructions"},
        {"CSR_SUC", PAPI_CSR_SUC,     "Successful store conditional instructions"},
        {"CSR_TOT", PAPI_CSR_TOT,     "Total store conditional instructions"},
        {"MEM_SCY", PAPI_MEM_SCY,     "Cycles Stalled Waiting for Memory Access"},
        {"MEM_RCY", PAPI_MEM_RCY,     "Cycles Stalled Waiting for Memory Read"},
        {"MEM_WCY", PAPI_MEM_WCY,     "Cycles Stalled Waiting for Memory Write"},
        {"STL_ICY", PAPI_STL_ICY,     "Cycles with No Instruction Issue"},
        {"FUL_ICY", PAPI_FUL_ICY,     "Cycles with Maximum Instruction Issue"},
        {"STL_CCY", PAPI_STL_CCY,     "Cycles with No Instruction Completion"},
        {"FUL_CCY", PAPI_FUL_CCY,     "Cycles with Maximum Instruction Completion"},
        {"HW_INT", PAPI_HW_INT,       "Hardware interrupts"},
        {"BR_UCN", PAPI_BR_UCN,       "Unconditional branch instructions executed"},
        {"BR_CN", PAPI_BR_CN,         "Conditional branch instructions executed"},
        {"BR_TKN", PAPI_BR_TKN,       "Conditional branch instructions taken"},
        {"BR_NTK", PAPI_BR_NTK,       "Conditional branch instructions not taken"},
        {"BR_MSP", PAPI_BR_MSP,       "Conditional branch instructions mispred"},
        {"BR_PRC", PAPI_BR_PRC,       "Conditional branch instructions corr. pred"},
        {"FMA_INS", PAPI_FMA_INS,     "FMA instructions completed"},
        {"TOT_IIS", PAPI_TOT_IIS,     "Total instructions issued"},
        {"TOT_INS", PAPI_TOT_INS,     "Total instructions executed"},
        {"INT_INS", PAPI_INT_INS,     "Integer instructions executed"},
        {"FP_INS", PAPI_FP_INS,       "Floating point instructions executed"},
        {"LD_INS", PAPI_LD_INS,       "Load instructions executed"},
        {"SR_INS", PAPI_SR_INS,       "Store instructions executed"},
        {"BR_INS", PAPI_BR_INS,       "Total branch instructions executed"},
        {"VEC_INS", PAPI_VEC_INS,     "Vector/SIMD instructions executed (could include integer)"},
        {"RES_STL", PAPI_RES_STL,     "Cycles processor is stalled on resource"},
        {"FP_STAL", PAPI_FP_STAL,     "Cycles any FP units are stalled"},
        {"TOT_CYC", PAPI_TOT_CYC,     "Total cycles executed"},
        {"LST_INS", PAPI_LST_INS,     "Total load/store inst. executed"},
        {"SYC_INS", PAPI_SYC_INS,     "Sync. inst. executed"},
        {"L1_DCH", PAPI_L1_DCH,       "L1 D Cache Hit"},
        {"L2_DCH", PAPI_L2_DCH,       "L2 D Cache Hit"},
        {"L1_DCA", PAPI_L1_DCA,       "L1 D Cache Access"},
        {"L2_DCA", PAPI_L2_DCA,       "L2 D Cache Access"},
        {"L3_DCA", PAPI_L3_DCA,       "L3 D Cache Access"},
        {"L1_DCR", PAPI_L1_DCR,       "L1 D Cache Read"},
        {"L2_DCR", PAPI_L2_DCR,       "L2 D Cache Read"},
        {"L3_DCR", PAPI_L3_DCR,       "L3 D Cache Read"},
        {"L1_DCW", PAPI_L1_DCW,       "L1 D Cache Write"},
        {"L2_DCW", PAPI_L2_DCW,       "L2 D Cache Write"},
        {"L3_DCW", PAPI_L3_DCW,       "L3 D Cache Write"},
        {"L1_ICH", PAPI_L1_ICH,       "L1 instruction cache hits"},
        {"L2_ICH", PAPI_L2_ICH,       "L2 instruction cache hits"},
        {"L3_ICH", PAPI_L3_ICH,       "L3 instruction cache hits"},
        {"L1_ICA", PAPI_L1_ICA,       "L1 instruction cache accesses"},
        {"L2_ICA", PAPI_L2_ICA,       "L2 instruction cache accesses"},
        {"L3_ICA", PAPI_L3_ICA,       "L3 instruction cache accesses"},
        {"L1_ICR", PAPI_L1_ICR,       "L1 instruction cache reads"},
        {"L2_ICR", PAPI_L2_ICR,       "L2 instruction cache reads"},
        {"L3_ICR", PAPI_L3_ICR,       "L3 instruction cache reads"},
        {"L1_ICW", PAPI_L1_ICW,       "L1 instruction cache writes"},
        {"L2_ICW", PAPI_L2_ICW,       "L2 instruction cache writes"},
        {"L3_ICW", PAPI_L3_ICW,       "L3 instruction cache writes"},
        {"L1_TCH", PAPI_L1_TCH,       "L1 total cache hits"},
        {"L2_TCH", PAPI_L2_TCH,       "L2 total cache hits"},
        {"L3_TCH", PAPI_L3_TCH,       "L3 total cache hits"},
        {"L1_TCA", PAPI_L1_TCA,       "L1 total cache accesses"},
        {"L2_TCA", PAPI_L2_TCA,       "L2 total cache accesses"},
        {"L3_TCA", PAPI_L3_TCA,       "L3 total cache accesses"},
        {"L1_TCR", PAPI_L1_TCR,       "L1 total cache reads"},
        {"L2_TCR", PAPI_L2_TCR,       "L2 total cache reads"},
        {"L3_TCR", PAPI_L3_TCR,       "L3 total cache reads"},
        {"L1_TCW", PAPI_L1_TCW,       "L1 total cache writes"},
        {"L2_TCW", PAPI_L2_TCW,       "L2 total cache writes"},
        {"L3_TCW", PAPI_L3_TCW,       "L3 total cache writes"},
        {"FML_INS", PAPI_FML_INS,     "FM ins"},
        {"FAD_INS", PAPI_FAD_INS,     "FA ins"},
        {"FDV_INS", PAPI_FDV_INS,     "FD ins"},
        {"FSQ_INS", PAPI_FSQ_INS,     "FSq ins"},
        {"FNV_INS", PAPI_FNV_INS,     "Finv ins"},
        {"FP_OPS", PAPI_FP_OPS,       "Floating point operations executed"},
        {"SP_OPS", PAPI_SP_OPS,       "Floating point operations executed; optimized to count scaled single precision vector operations"},
        {"DP_OPS", PAPI_DP_OPS,       "Floating point operations executed; optimized to count scaled double precision vector operations"},
        {"VEC_SP", PAPI_VEC_SP,       "Single precision vector/SIMD instructions"},
        {"VEC_DP", PAPI_VEC_DP,       "Double precision vector/SIMD instructions"},
        {"REF_CYC", PAPI_REF_CYC,     "Reference clock cycles"}
};

HardwareCounter::HardwareCounter()
{
}

HardwareCounter::~HardwareCounter()
{
}
int
HardwareCounter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    _events.resize(conf.size());
    for (int i = 0; i < conf.size(); i++) {
        int idx = -1;
        for (int j = 0; j < sizeof(known_events) / sizeof(event); j++) {
            event kevent = known_events[j];
            if (conf[i] == kevent.name) {
                idx = j;
                break;
            }
        }

        if (idx == -1) {
            return errh->error("Unknown event %s",conf[i].c_str());
        }
        _events[i] = known_events[idx];
    }

    _accum_values.resize(conf.size(), 0);
    return 0;
}

int
HardwareCounter::initialize(ErrorHandler *errh)
{
    long_long values[_events.size()];

    /* Start counting events */
    int err;
    int elist[_events.size()];
    for (int i = 0; i < _events.size(); i++) {
        elist[i] = _events[i].idx;
    }
    if ((err = PAPI_start_counters(elist, _events.size())) != PAPI_OK)
       errh->error("Could not start counters : error %d", err);

    /* Do some computation here */


    return 0;
}

void
HardwareCounter::cleanup(CleanupStage)
{
    long long values[_events.size()];
    /* Stop counting events */
    if (PAPI_stop_counters(values, _events.size()) != PAPI_OK)
       click_chatter("Could not stop counters");
}

String
HardwareCounter::read_handler(Element *e, void *thunk)
{
    HardwareCounter *fd = static_cast<HardwareCounter *>(e);

    int sz = fd->_events.size();

    int idx = (intptr_t)thunk;
    if (idx == h_dump_read) {
        long long values[sz];

        /* Read the counters */
        if (PAPI_read_counters(values, sz) != PAPI_OK)
           return "<error>";

        StringAccum acc;
        for (int i = 0; i < sz; i++) {
            acc << fd->_events[i].name << " : " << String(values[i]) << "\n";
        }
        return acc.take_string();
    } else if (idx == h_dump_accum) {
        StringAccum acc;
        for (int i = 0; i < sz; i++) {
            acc << fd->_events[i].name << " : " << String(fd->_accum_values[i]) << "\n";
        }
        return acc.take_string();
    } else if (idx == h_list) {
        StringAccum acc;
        for (auto kevent : known_events) {
            acc << kevent.name << " : " << kevent.desc << "\n";
        }
        return acc.take_string();
    }

    if (idx < 0 || idx >= sz) {
        return "<error>";
    }

    return String(fd->_accum_values[idx]);
}

int
HardwareCounter::write_handler(const String &data, Element *e,
                    void *thunk, ErrorHandler *errh) {
    HardwareCounter *fd = static_cast<HardwareCounter *>(e);

    int sz = fd->_events.size();
    int idx = (intptr_t)thunk;
    if (idx == h_accum) {
            /* Read the counters */
            if (PAPI_accum_counters(fd->_accum_values.data(), sz) != PAPI_OK)
               return errh->error("Could not accumulate values");
            return 0;
    } else if (idx == h_reset_accum) {
        for (int i = 0; i < sz; i++) {
            fd->_accum_values[i] = 0;
        }
    } else {
        return errh->error("Unknown request");
    }

}


void
HardwareCounter::add_handlers()
{
    add_read_handler("dump_read", read_handler, h_dump_read, 0);
    add_read_handler("dump_accum", read_handler, h_dump_accum, 0);
    add_write_handler("accum", write_handler, h_accum, 0);
    add_write_handler("reset_accum", write_handler, h_reset_accum, 0);
    add_read_handler("list", read_handler, h_list, 0);

    add_read_handler("accum_0", read_handler, 0, 0);
    add_read_handler("accum_1", read_handler, 1, 0);
    add_read_handler("accum_2", read_handler, 2, 0);
    add_read_handler("accum_3", read_handler, 3, 0);
    add_read_handler("accum_4", read_handler, 4, 0);
    add_read_handler("accum_5", read_handler, 5, 0);
    add_read_handler("accum_6", read_handler, 6, 0);
    add_read_handler("accum_7", read_handler, 7, 0);
}


CLICK_ENDDECLS
ELEMENT_LIBS(-lpapi)
ELEMENT_REQUIRES(papi)
EXPORT_ELEMENT(HardwareCounter)
ELEMENT_MT_SAFE(HardwareCounter)
