/*
 * frommindump.{cc,hh} - Generates packets from minimal size traces
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
#include "frommindump.hh"
#include "tomindump.hh"
#include <click/args.hh>
#include <click/router.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/straccum.hh>
#include <click/dpdkdevice.hh>
#include <click/userutils.hh>
#include <elements/standard/script.hh>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <rte_malloc.h>


CLICK_DECLS

FromMinDump::FromMinDump() : _task(this), _first_packet(0) {
#if HAVE_BATCH
  in_batch_mode = BATCH_MODE_YES;
#endif
}

FromMinDump::~FromMinDump() {}

int FromMinDump::configure(Vector<String> &conf, ErrorHandler *errh) {
  String loop_call = "";

  if (Args(conf, this, errh)
          .read_p("FILENAME", _filename)
          .read_or_set("STOP", _stop, 1)
          .read_or_set("ACTIVE", _active, 1)
          .read_or_set("BURST", _burst, 32)
          .read_or_set("VERBOSE", _verbose, 0)
          .read_or_set("LIMIT", _limit, -1) // By default read all the file
          .read_or_set("TIMES", _times, 1) // For 1 time only. -1 is infinite loop
          .read_or_set("LOOP_CALL", loop_call, "")
          .read_or_set("DPDK", _dpdk, false)

          .complete() < 0)
    return -1;

  _times--; // It's like a do...while
  _this_limit = _limit;

  if (loop_call != "")
    _loop_trigger_h = new HandlerCall(loop_call);
  else
    _loop_trigger_h = 0;

  // TODO: Check if file was given and that it exists
  // TODO: check that a DPDK pool has been createed

  return 0;
}

int FromMinDump::initialize(ErrorHandler *errh) {
  if (output_is_push(0))
    ScheduleInfo::initialize_task(this, &_task, _active, errh);
  else
    _notifier.initialize(Notifier::EMPTY_NOTIFIER, router());

  if (_loop_trigger_h)
    if (_loop_trigger_h->initialize_write(this, errh) < 0)
      return -1;

  if(_verbose)
    click_chatter("File name is %s", _filename.c_str());

  _f = fopen(_filename.c_str(), "rb");
  if (!_f)
    return errh->error("Error while opening file");
  else if(_verbose)
    click_chatter("Openened file %s", _filename.c_str());

  _file_pos = 0;

  fseek(_f, 0, SEEK_END);
  _file_size = ftell(_f);
  if(_verbose)
    click_chatter("The file is %i byte", _file_size);
  _file_data = (uint8_t *)malloc(_file_size);
  //_file_data = (uint8_t *) rte_malloc("frommindump", _file_size, 0);

  if (!_file_data)
    return errh->error("Could not allocate memory to preload file!");

  fseek(_f, 0, SEEK_SET);
  size_t ret = fread(_file_data, 1, _file_size, _f);
  if (ret != _file_size)
    return errh->error("Error while reading the file in memory to %p (read %lld "
                       "bytes, expected %lld)!",
                       _file_data, ret, _file_size);

  fclose(_f);
  _f = 0;

  char buff[65536];
  _first_packet = 0;
  int guard = 0;
  while (!_first_packet) {
    char *ret = read_string_line(buff);
    if (!ret)
      return errh->error("Error while reading file");
    if (!strcmp(buff, "!data\n")) {
      _first_packet = _file_pos;
    } else {
	if(_verbose)
	    click_chatter("Read metadata from file: %s", buff);
      // TODO: check file version and metadata
    }

    // Avoid to read forever the file in case of wrong format
    guard++;
    if (guard > 1000)
      return errh->error("Loop detected at the beginning of the file!");
  }

  if(_verbose)
    click_chatter("DATA starts at %i", _first_packet);

  return 0;
}

void FromMinDump::cleanup(CleanupStage) {
  if (_f) {
    fclose(_f);
    _f = 0;
  }
  if (_file_data) {
    free(_file_data);
    _file_data = 0;
  }
}

Packet *FromMinDump::read_packet(ErrorHandler *errh, uint8_t *data) {
  WritablePacket *p;
  int packet_len = *ASWORDP(data + LEN_OFF);
  if (likely(_dpdk)) {
    int _node = 0; // TODO
    rte_mbuf *mb = DPDKDevice::get_pkt(_node);
    assert(mb);
    unsigned char *pdata = rte_pktmbuf_mtod(mb, unsigned char *);
    //p = Packet::make(rte_pktmbuf_headroom(mb), mb, packet_len, rte_pktmbuf_tailroom(mb));
    
    p = Packet::make(pdata, packet_len, DPDKDevice::free_pkt, mb,
                     rte_pktmbuf_headroom(mb), rte_pktmbuf_tailroom(mb));



  } else {
    p = Packet::make(14, 0, packet_len, 0);
  }

  p->set_network_header(p->data(), sizeof(click_ip));
  click_ip *iph = p->ip_header();
  iph->ip_v = 4;
  iph->ip_hl = sizeof(click_ip) >> 2;
  iph->ip_p = FLAGS2PROTO(*(data + FLAGS_OFF));
  iph->ip_len = htons(packet_len);
  iph->ip_off = 0;
  iph->ip_ttl = 100;
  iph->ip_v = 4;
  iph->ip_id = 0;
  iph->ip_tos = 0;
  iph->ip_sum = 0;
  // We let the NIC or another element to calculate checksums
  // ip->ip_sum = click_in_cksum((unsigned char *)ip, sizeof(click_ip));

  memcpy(&(iph->ip_src.s_addr), data + IPSRC_OFF, IP_L);
  memcpy(&(iph->ip_dst.s_addr), data + IPDST_OFF, IP_L);

  int proto = FLAGS2PROTO(*(data + FLAGS_OFF));
  iph->ip_p = proto;

  if (proto == IP_PROTO_TCP) {
    click_tcp *tcp = reinterpret_cast<click_tcp *>(iph + 1);
    tcp->th_sport = *ASWORDP(SPORT_OFF + data);
    tcp->th_dport = *ASWORDP(DPORT_OFF + data);
    tcp->th_sum = 0;
    tcp->th_seq = 1; // click_random();
    tcp->th_ack = 0; // click_random();
    tcp->th_off = sizeof(click_tcp) >> 2;
    tcp->th_win = 65535;
    tcp->th_urp = 0;
    tcp->th_sum = 0;
    // unsigned csum = click_in_cksum((uint8_t *)tcp, packet_len);
    // tcp->th_sum = click_in_cksum_pseudohdr(csum, iph, packet_len);
  } else if (proto == IP_PROTO_UDP) {

    click_udp *udp = reinterpret_cast<click_udp *>(iph + 1);

    udp->uh_sport = *ASWORDP(SPORT_OFF + data);
    udp->uh_dport = *ASWORDP(DPORT_OFF + data);
    udp->uh_sum = 0;
    udp->uh_ulen = htons(packet_len);
    // unsigned short len = packet_len; //-sizeof(click_ip);
    // unsigned csum = click_in_cksum((uint8_t *)udp, packet_len);
    // udp->uh_sum = click_in_cksum_pseudohdr(csum, iph, packet_len);
  }

  return p;
}

inline int FromMinDump::read_binary_line(uint8_t *buffer) {
  if (likely(_file_pos + LINE_LEN < _file_size)) {
    // TODO avoid memcpy, read directly from the buffer
    memcpy(buffer, _file_data + _file_pos, LINE_LEN);
    _file_pos += LINE_LEN;
    return LINE_LEN;
  } else {
    memcpy(buffer, _file_data + _file_pos, _file_size - _file_pos);
    _file_pos = _file_size;
    return _file_size - _file_pos;
  }
}

inline char *FromMinDump::read_string_line(char *s) {

  int ret = sscanf((char *)(_file_data + _file_pos), "%65535[^\n]", s);
  if (unlikely(ret == EOF)) {
    click_chatter("Error while getting string from the file: %i %s", errno,
                  strerror(errno));
    return 0;
  }
  // Add the new line, as it would be returned by fgets
  ret = strlen(s);
  s[ret] = '\n';
  s[ret + 1] = 0;
  ret += 1;

  _file_pos += ret;
  // TODO CHECK ERRORS
  return s;
}
inline int FromMinDump::go_to_first_packet() {
  // Further controls may be added here
  _file_pos = _first_packet;
  return 0;
}

// Helper to rewind the file to the beginning
// Return 0 in case of error, 1 in case of success
inline bool FromMinDump::fileRewind() {
  if (unlikely(!_times)) {
    // No other packets to produce
    click_chatter("No more packets");
    return 0; // we have finished the reading
  }
  // We have to return to the beginning of the file
  if (_times > 0)
    _times--;
  _this_limit = _limit;
  int ret = go_to_first_packet();
  if (ret) {
    click_chatter("Error while looping over file: %s", strerror(errno));
    return 0;
  }
  if (unlikely(_verbose))
    click_chatter("Loop!");
  if (_loop_trigger_h)
    _loop_trigger_h->call_write();

  return 1;
}

inline Packet *FromMinDump::get_packet(bool push) {

  uint8_t data[LINE_LEN];
  int ret = read_binary_line(data);
  if (ret < LINE_LEN || _this_limit == 0)
    if (fileRewind()) {
      ret = read_binary_line(data);
      if (ret < LINE_LEN)
        return 0;
    } else {
      return 0;
    }

  Packet *p = read_packet(0, data);

  if (_this_limit > 0)
    _this_limit--;

  if (!p) {
    if (_stop)
      router()->please_stop_driver();
    if (!push)
      _notifier.sleep();
    return 0;
  }
  return p;
}

void FromMinDump::run_timer(Timer *) {
  if (_active) {
    if (output_is_pull(0)) {
      _notifier.wake();
    } else
      _task.reschedule();
  }
}

bool FromMinDump::run_task(Task *) {
  if (!_active)
    return false;

#if HAVE_BATCH
  PacketBatch *head = NULL;
  Packet *last = NULL;
  Packet *p = NULL;
  for (int i = 0; i < _burst; i++) {
    p = get_packet(1);
    if (likely(p)) {
      if (unlikely(head == NULL)) {
        head = PacketBatch::start_head(p);
        last = p;
      } else {
        last->set_next(p);
        last = last->next();
      }
    } else
      break;
  }
  if (likely(head))
    output_push_batch(0, head->make_tail(last, _burst));
#else
  Packet *p = 0;
  for (int i = 0; i < _burst; i++) {
    p = get_packet(1);
    output(0).push(p);
  }
#endif

  if (_active)
    if (likely((_limit || (_limit == 0 && _times)) && p)) {
      _task.fast_reschedule();
    } else if ((_limit == 1 || !p) && _stop) {
      click_chatter("Generation finished. Asking the driver to stop.");
      router()->please_stop_driver();
    }
  return true;
}

Packet *FromMinDump::pull(int) {
  if (!_active)
    return 0;
  Packet *p = get_packet();
  if (p)
    _notifier.wake();
  else if (_stop)
    router()->please_stop_driver();
  return p;
}

#if HAVE_BATCH
PacketBatch *FromMinDump::pull_batch(int, unsigned max) {
  if (!_active)
    return 0;
  PacketBatch *batch = 0;
  MAKE_BATCH(get_packet(), batch, max);
  if (!batch) {
    if (_stop)
      router()->please_stop_driver();
    return 0;
  }
  if (batch->count() == max) {
    _notifier.wake();
  }
  return batch;
}
#endif

enum { H_ACTIVE, H_STOP };

String FromMinDump::read_handler(Element *e, void *thunk) {
  FromMinDump *fd = static_cast<FromMinDump *>(e);
  switch ((intptr_t)thunk) {
  case H_ACTIVE:
    return BoolArg::unparse(fd->_active);
  default:
    return "<error>";
  }
}

int FromMinDump::write_handler(const String &s_in, Element *e, void *thunk,
                               ErrorHandler *errh) {
  FromMinDump *fd = static_cast<FromMinDump *>(e);
  String s = cp_uncomment(s_in);
  switch ((intptr_t)thunk) {
  case H_ACTIVE: {
    bool active;
    if (BoolArg().parse(s, active)) {
      fd->_active = active;
      if (fd->output_is_push(0) && active && !fd->_task.scheduled())
        fd->_task.reschedule();
      else if (!fd->output_is_push(0))
        fd->_notifier.set_active(active, true);
      return 0;
    } else
      return errh->error("type mismatch");
  }
  case H_STOP:
    fd->_active = false;
    fd->router()->please_stop_driver();
    return 0;
  default:
    return -EINVAL;
  }
}

void FromMinDump::add_handlers() {
  add_read_handler("active", read_handler, H_ACTIVE, Handler::f_checkbox);
  add_write_handler("active", write_handler, H_ACTIVE);
  add_write_handler("stop", write_handler, H_STOP, Handler::f_button);
  if (output_is_push(0))
    add_task_handlers(&_task);
}



ELEMENT_REQUIRES(userlevel dpdk)
EXPORT_ELEMENT(FromMinDump)
CLICK_ENDDECLS
