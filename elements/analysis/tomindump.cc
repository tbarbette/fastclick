/*
 * tomindump.{cc,hh} - Record packets into minimal size traces
 *
 * Copyright (c) 2020 Massimo Girondi, KTH Royal Institute of Technology
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
#include "tomindump.hh"
#include <click/standard/scheduleinfo.hh>
#include <click/args.hh>
#include <click/error.hh>

CLICK_DECLS

ToMinDump::ToMinDump() : _f(0), _task(this) {}

ToMinDump::~ToMinDump() {}

int ToMinDump::configure(Vector<String> &conf, ErrorHandler *errh) {
  bool verbose = false;

  if (Args(conf, this, errh)
          .read_mp("FILENAME", FilenameArg(), _filename)
          .read("VERBOSE", verbose)
          .complete() < 0)
    return -1;

  _verbose = verbose;

  return errh->nerrors() ? -1 : 0;
}

int ToMinDump::initialize(ErrorHandler *errh) {
  assert(!_f);
  if (_filename != "-") {
    _f = fopen(_filename.c_str(), "wb");
    if (!_f)
      return errh->error("%s: %s", _filename.c_str(), strerror(errno));
  } else {
    _f = stdout;
    _filename = "<stdout>";
  }

  if (input_is_pull(0)) {
    ScheduleInfo::join_scheduler(this, &_task, errh);
    _signal = Notifier::upstream_empty_signal(this, 0, &_task);
  }
  _output_count = 0;

  // magic number
  StringAccum sa;
  sa << "!MINDUMP" << MINDUMP_MAJOR_VERSION << '.' << MINDUMP_MINOR_VERSION
     << '\n';
  sa << "!data\n";

  ignore_result(fwrite(sa.data(), 1, sa.length(), _f));

  return 0;
}

void ToMinDump::cleanup(CleanupStage) {
  if (_f && _f != stdout)
    fclose(_f);
  _f = 0;
}

void ToMinDump::write_packet(Packet *packet) {

  uint8_t data[LINE_LEN];
  const uint8_t *p = ASCBYTEP(packet->data());

  int eth_len = packet->length();

  // We save IPs and ports in Network order to avoid conversion during
  // generation Length is in host order
  memcpy(data + IPSRC_OFF, P_IPSRC_OFF + p, IP_L);
  memcpy(data + IPDST_OFF, P_IPDST_OFF + p, IP_L);
  memcpy(data + SPORT_OFF, P_SPORT_OFF + p, PORT_L);
  memcpy(data + DPORT_OFF, P_DPORT_OFF + p, PORT_L);
  *ASWORDP(data + LEN_OFF) = packet->length();
  *ASWORDP(data + FLAGS_OFF) = PROTO2FLAGS(*(p + P_PROTO_OFF));

  fwrite(data, 1, LINE_LEN, _f);
  _output_count++;
}

void ToMinDump::push(int, Packet *p) {
  write_packet(p);
  checked_output_push(0, p);
}

bool ToMinDump::run_task(Task *) {
  if (Packet *p = input(0).pull()) {
    write_packet(p);
    checked_output_push(0, p);
    _task.fast_reschedule();
    return true;
  } else if (_signal) {
    _task.fast_reschedule();
    return false;
  } else
    return false;
}

int ToMinDump::flush_handler(const String &, Element *e, void *,
                             ErrorHandler *) {
  ToMinDump *tod = (ToMinDump *)e;
  if (tod->_f)
    fflush(tod->_f);
  return 0;
}

void ToMinDump::add_handlers() {
  if (input_is_pull(0))
    add_task_handlers(&_task);
  add_write_handler("flush", flush_handler);
}
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(ToMinDump)
CLICK_ENDDECLS
