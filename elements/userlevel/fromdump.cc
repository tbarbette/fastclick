// -*- mode: c++; c-basic-offset: 4 -*-
/*
 * fromdump.{cc,hh} -- element reads packets from tcpdump file
 * Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 * Copyright (c) 2001-2003 International Computer Science Institute
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
#include "fromdump.hh"
#include <click/args.hh>
#include <click/router.hh>
#include <click/routervisitor.hh>
#include <click/straccum.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/glue.hh>
#include <click/handlercall.hh>
#include <click/packet_anno.hh>
#include <click/userutils.hh>
#include <clicknet/ether.h>
#if CLICK_NS
# include <click/master.hh>
#endif
#include "fakepcap.hh"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef ALLOW_MMAP
#include <sys/mman.h>
#endif
#if HAVE_BATCH
#include <click/batchbuilder.hh>
#endif

CLICK_DECLS

#define	SWAPLONG(y) \
	((((y)&0xff)<<24) | (((y)&0xff00)<<8) | (((y)&0xff0000)>>8) | (((y)>>24)&0xff))
#define	SWAPSHORT(y) \
	( (((y)&0xff)<<8) | ((u_short)((y)&0xff00)>>8) )

#define MIN_MTU 60
#define MAX_MTU 9000

FromDump::FromDump()
    : _packet(0), _preload(0), _preload_head(0), _force_len(DISABLED), _end_h(0), _count(0),  _timer(this), _task(this)
{
    in_batch_mode = BATCH_MODE_YES;
}

FromDump::~FromDump()
{
    delete _end_h;
}

void *
FromDump::cast(const char *n)
{
    if (strcmp(n, Notifier::EMPTY_NOTIFIER) == 0 && !output_is_push(0))
	return static_cast<Notifier *>(&_notifier);
    else
	return Element::cast(n);
}

String
FromDump::declaration() const
{
    StringAccum sa;
    sa << name() << " :: " << class_name();
    if (_ff.initialized())
	sa << '(' << _ff.print_filename() << ')';
    return sa.take_string();
}

int
FromDump::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int force_len = DISABLED;
    bool timing = false, stop = false, active = true, force_ip = false;
    Timestamp first_time, first_time_off, last_time, last_time_off, interval;
    HandlerCall end_h;
    _sampling_prob = (1 << SAMPLING_SHIFT);
#if CLICK_NS
    bool per_node = false;
#endif
    _packet_filepos = 0;
    _preload = 0;
    String timing_fnt;

    if (_ff.configure_keywords(conf, this, errh) < 0)
	return -1;
    if (Args(conf, this, errh)
    .read_mp("FILENAME", FilenameArg(), _ff.filename())
    .read_p("TIMING", timing)
    .read("STOP", stop)
    .read("ACTIVE", active)
    .read("SAMPLE", FixedPointArg(SAMPLING_SHIFT), _sampling_prob)
    .read("FORCE_IP", force_ip)
    .read("FORCE_LEN", force_len)
    .read("START", first_time)
    .read("START_AFTER", first_time_off)
    .read("END", last_time)
    .read("END_AFTER", last_time_off)
    .read("INTERVAL", interval)
    .read("END_CALL", HandlerCallArg(HandlerCall::writable), end_h)
#if CLICK_NS
    .read("PER_NODE", per_node)
#endif
    .read("FILEPOS", _packet_filepos)
    .read("PRELOAD", _preload)
    .read_or_set("ACCELERATION", _current_accel, 100)
    .read_or_set("TIMING_FNT", timing_fnt, "")
    .read_or_set("BURST", _burst, 32)
    .complete() < 0)
	return -1;

    // check sampling rate
    if (_sampling_prob > (1 << SAMPLING_SHIFT)) {
	errh->warning("SAMPLE probability reduced to 1");
	_sampling_prob = (1 << SAMPLING_SHIFT);
    } else if (_sampling_prob == 0)
	errh->warning("SAMPLE probability is 0; emitting no packets");

    // check times
    _have_first_time = _have_last_time = true;
    _first_time_relative = _last_time_relative = _last_time_interval = false;

    if ((bool) first_time + (bool) first_time_off > 1)
	return errh->error("START and START_AFTER are mutually exclusive");
    else if (first_time)
	_first_time = first_time;
    else if (first_time_off)
	_first_time = first_time_off, _first_time_relative = true;
    else
	_have_first_time = false, _first_time_relative = true;

    if ((bool) last_time + (bool) last_time_off + (bool) interval > 1)
	return errh->error("END, END_AFTER, and INTERVAL are mutually exclusive");
    else if (last_time)
	_last_time = last_time;
    else if (last_time_off)
	_last_time = last_time_off, _last_time_relative = true;
    else if (interval)
	_last_time = interval, _last_time_interval = true;
    else
	_have_last_time = false;

    if (stop && end_h)
	return errh->error("END_CALL and STOP are mutually exclusive");
    else if (end_h)
	_end_h = new HandlerCall(end_h);
    else if (stop)
	_end_h = new HandlerCall(name() + ".stop");
    else if (_have_last_time)
	_end_h = new HandlerCall(name() + ".active false");

    // set other variables
    _have_any_times = false;
    _timing = timing;
    _force_ip = force_ip;
    _force_len = force_len;

    if ((_force_len != DISABLED) && (_force_len != REAL_LEN) &&
       ((_force_len < MIN_MTU) || (_force_len > MAX_MTU))) {
        return errh->error("FORCE_LEN requires a valid frame size");
    }

    // User feedback regarding a potential frame length manipulation
    if (_force_len == DISABLED) {
        auto t = ElementCastTracker(router(), "Pad");
        int ok = router()->visit_downstream(this, 0, &t);

        if (ok != 0 || t.elements().size() < 0)
            errh->warning(
                "Frames will be loaded with their capture lengths; "
                "this might cause issues when the length of the capture is different from the actual frame length. \n"
                "Use Pad() to avoid frame drops, e.g., caused by a subsequent CheckIPHeader element."
            );
    } else if (_force_len == REAL_LEN) {
        errh->warning(
            "Frames will be loaded with their actual lengths; "
            "this will likely slow down the injection rate due to frame padding/chopping operations."
        );
    } else {
        errh->warning(
            "Frames will be loaded with a fixed length of %d bytes; "
            "this will likely slow down the injection rate due to frame padding/chopping operations.", _force_len
        );
    }

    if (timing_fnt != "") {
        _fnt_expr = TinyExpr::compile(timing_fnt, 1);
    }

    if (_timing && _preload > 0) {
        return errh->error("PRELOAD is not compatible with TIMING");
    }

#if CLICK_NS
    if (per_node) {
	char tmp[255];
	int r = simclick_sim_command(router()->simnode(), SIMCLICK_GET_NODE_NAME, tmp,255);
	if (r >= 0)
	    _ff.filename() = String(tmp) + String("_") + _ff.filename();
    }
#endif

    _active = active;
    return 0;
}

static void
swap_file_header(const fake_pcap_file_header *hp, fake_pcap_file_header *outp)
{
    outp->magic = SWAPLONG(hp->magic);
    outp->version_major = SWAPSHORT(hp->version_major);
    outp->version_minor = SWAPSHORT(hp->version_minor);
    outp->thiszone = SWAPLONG(hp->thiszone);
    outp->sigfigs = SWAPLONG(hp->sigfigs);
    outp->snaplen = SWAPLONG(hp->snaplen);
    outp->linktype = SWAPLONG(hp->linktype);
}

static void
swap_packet_header(const fake_pcap_pkthdr *hp, fake_pcap_pkthdr *outp)
{
    outp->ts.tv.tv_sec = SWAPLONG(hp->ts.tv.tv_sec);
    outp->ts.tv.tv_usec = SWAPLONG(hp->ts.tv.tv_usec);
    outp->caplen = SWAPLONG(hp->caplen);
    outp->len = SWAPLONG(hp->len);
}

FromDump *
FromDump::hotswap_element() const
{
    if (Element *e = Element::hotswap_element())
	if (FromDump *fd = static_cast<FromDump *>(e->cast("FromDump")))
	    if (fd->_ff.filename() == _ff.filename())
		return fd;
    return 0;
}

int
FromDump::initialize(ErrorHandler *errh)
{
    // make sure notifier is initialized
    if (!output_is_push(0))
	_notifier.initialize(Notifier::EMPTY_NOTIFIER, router());

    // check handler call, initialize Task
    if (_end_h && _end_h->initialize_write(this, errh) < 0)
	return -1;
    if (output_is_push(0))
	ScheduleInfo::initialize_task(this, &_task, _active, errh);
    _timer.initialize(this);

    // skip if hotswapping
    if (hotswap_element())
	return 0;

    // open file
    if (_ff.initialize(errh) < 0)
	return -1;

    // check magic number
    fake_pcap_file_header swapped_fh;
    const fake_pcap_file_header *fh = (const fake_pcap_file_header *)_ff.get_aligned(sizeof(fake_pcap_file_header), &swapped_fh);
    if (!fh)
	return _ff.error(errh, "not a tcpdump file (too short)");

    if (fh->magic == FAKE_PCAP_MAGIC || fh->magic == FAKE_PCAP_MAGIC_NANO || fh->magic == FAKE_MODIFIED_PCAP_MAGIC)
	    _swapped = false;
    else {
        swap_file_header(fh, &swapped_fh);
        _swapped = true;
        fh = &swapped_fh;
    }
    if (fh->magic != FAKE_PCAP_MAGIC && fh->magic != FAKE_PCAP_MAGIC_NANO && fh->magic != FAKE_MODIFIED_PCAP_MAGIC)
	    return _ff.error(errh, "not a tcpdump file (bad magic number)");
    // compensate for extra crap appended to packet headers
    if (fh->magic == FAKE_PCAP_MAGIC || fh->magic == FAKE_PCAP_MAGIC_NANO)
	    _extra_pkthdr_crap = 0;
    else
	    _extra_pkthdr_crap = sizeof(fake_modified_pcap_pkthdr) - sizeof(fake_pcap_pkthdr);
    _have_nanosecond_timestamps = fh->magic == FAKE_PCAP_MAGIC_NANO;

    if (fh->version_major != FAKE_PCAP_VERSION_MAJOR)
	    return _ff.error(errh, "unknown major version %d", fh->version_major);

    _minor_version = fh->version_minor;
    // map possible host link types to global link types
    _linktype = fake_pcap_canonical_dlt(fh->linktype, true);

    // if forcing IP packets, check datalink type to ensure we understand it
    if (_force_ip) {
	   if (!fake_pcap_dlt_force_ipable(_linktype))
	       return _ff.error(errh, "unknown linktype %d; can't force IP packets", _linktype);
    } else if (_linktype == FAKE_DLT_RAW) {
        // force FORCE_IP.
        _force_ip = true;
    }

    // maybe skip ahead in the file
    int result;
    if (_packet_filepos != 0) {
	result = _ff.seek(_packet_filepos, errh);
	_packet_filepos = 0;
    } else
	result = 0;

    if (_preload) {
        Packet* _preload_tail = 0;
    	if (_preload < 0)
    		_preload = LONG_MAX;
    	int p_count = 0;
    	int b_count = 0;
    	while (_preload > 0) {
    		read_packet(errh);
    		if (_packet == 0) {
    			errh->warning("Cannot read more than %d packets because no more Packet objects could be allocated or trace is finished.");
    			break;
    		}

    		if (_preload_tail)  {
    			_preload_tail->set_next(_packet);
    			_preload_tail = _packet;
    		} else {
    			_preload_head = _packet;
    			_preload_tail = _packet;
    		}
    		_preload-=_packet->length();
    		b_count += _packet->length();
    		_packet = 0;
    		p_count++;
    	}
    	click_chatter("%s : Preloaded %d packets (%d bytes)",name().c_str(),p_count,b_count);
    }
    return result;
}

void
FromDump::take_state(Element *e, ErrorHandler *errh)
{
    FromDump *o = static_cast<FromDump *>(e); // checked by hotswap_element()

    _ff.take_state(o->_ff, errh);

    _packet = o->_packet;
    o->_packet = 0;

    _swapped = o->_swapped;
    _extra_pkthdr_crap = o->_extra_pkthdr_crap;
    _minor_version = o->_minor_version;

    _linktype = o->_linktype;
    if (_linktype == FAKE_DLT_RAW)
	_force_ip = true;
    else if (_force_ip && !fake_pcap_dlt_force_ipable(_linktype))
	_ff.warning(errh, "unknown linktype %d; can't force IP packets", _linktype);

    _timing_offset = o->_timing_offset;
    _packet_filepos = o->_packet_filepos;
}

void
FromDump::cleanup(CleanupStage)
{
    _ff.cleanup();
    if (_packet)
	_packet->kill();
    _packet = 0;
}

void
FromDump::set_active(bool active)
{
    _active = active;
    if (active) {
	if (output_is_push(0) && !_task.scheduled())
	    _task.reschedule();
	else if (!output_is_push(0))
	    _notifier.wake();
    }
}

void
FromDump::prepare_times(const Timestamp &ts)
{
    if (_first_time_relative)
	_first_time += ts;
    if (_last_time_relative)
	_last_time += ts;
    else if (_last_time_interval)
	_last_time += _first_time;
    if (_timing) {
	    _timing_offset = ts;
        _starttime = Timestamp::now_steady();
        _last_real = _starttime;
        _last_check = 0;
        if (_fnt_expr)
            _current_accel = _fnt_expr.eval(0);
    }
     _have_any_times = true;
}

bool
FromDump::read_packet(ErrorHandler *errh)
{
    fake_pcap_pkthdr swapped_ph;
    const fake_pcap_pkthdr *ph;
    Timestamp ts = Timestamp::uninitialized_t();
    int len, caplen, skiplen = 0;
    Packet *p;
    assert(!_packet);

	if (!errh && _preload_head) {
		_packet = _preload_head;
		_preload_head = _preload_head->next();
		return true;
	}

    // record file position
    _packet_filepos = _ff.file_pos();

    // read the packet header
    if (!(ph = reinterpret_cast<const fake_pcap_pkthdr *>(_ff.get_aligned(sizeof(*ph), &swapped_ph))))
	    return false;
    if (_swapped) {
        swap_packet_header(ph, &swapped_ph);
        ph = &swapped_ph;
    }

    // may need to swap 'caplen' and 'len' fields at or before version 2.3
    if (_minor_version > 3 || (_minor_version == 3 && ph->caplen <= ph->len)) {
        len = ph->len;
        caplen = ph->caplen;
    } else {
        len = ph->caplen;
        caplen = ph->len;
    }

    // check for errors
    // 3.Jul.2002 -- Angelos Stavrou discovered that tcptrace-generated
    // tcpdump files store an incorrect caplen. It's only off by one. Tcptrace
    // should be fixed, but we hack around the problem here, as does
    // tcpdump itself.
    if (caplen > 65535) {
	    _ff.error(errh, "bad packet header; giving up");
	    return false;
    } else if (caplen > len) {
        skiplen = caplen - len;
        caplen = len;
    }

    // compensate for modified pcap versions
    _ff.shift_pos(_extra_pkthdr_crap);

    // check times
  check_times:
    ts = fake_bpf_timeval_union::make_timestamp(&ph->ts, _have_nanosecond_timestamps);
    if (!_have_any_times)
	prepare_times(ts);
    if (_have_first_time) {
	if (ts < _first_time) {
	    _ff.shift_pos(caplen + skiplen);
	    return true;
	} else
	    _have_first_time = false;
    }
    if (_have_last_time && ts >= _last_time) {
        _have_last_time = false;
        (void) _end_h->call_write(errh);
        if (!_active) {
            _ff.shift_pos(caplen + skiplen);
            return false;
        }
        // retry _last_time in case someone changed it
        goto check_times;
    }

    // checking sampling probability
    if (_sampling_prob < (1 << SAMPLING_SHIFT)
        && (click_random() & ((1<<SAMPLING_SHIFT)-1)) >= _sampling_prob) {
        _ff.shift_pos(caplen + skiplen);
        return true;
    }

    // create packet
    p = _ff.get_packet(caplen, ts.sec(), ts.subsec(), errh);
    if (!p)
	    return false;

    // Adjust the packet length as requested by the user
    if (_force_len != DISABLED) {
        WritablePacket *q = 0;
        int desired_len = -1;

        // User asked for the real length of the packet.
        // In the case of raw IP packets we reduce the length as they lack an Ethernet header
        if (_force_len == REAL_LEN) {
            desired_len = (_linktype == FAKE_DLT_RAW) ? (len - 14) : len;
        // .. or a given legnth
        } else {
            desired_len = (_linktype == FAKE_DLT_RAW) ? (_force_len - 14) : _force_len;
        }

        // Need to enlarge the packet
        if (desired_len > caplen) {
            q = p->put(desired_len - caplen);
        // Need to chop the packet
        } else {
            q = p->uniqueify();
            q->take(caplen - desired_len);
        }

        if (!q) {
            p->kill();
            _packet = 0;
            return false;
        }

        // Recompute the IP checksum
        click_ip *ip = 0;
        int offset = 0;
        if (_linktype == FAKE_DLT_RAW) {
            ip = reinterpret_cast<click_ip *>(q->data());
        } else {
            ip = reinterpret_cast<click_ip *>(q->data() + sizeof(click_ether));
            offset = 14;
        }

        if (!ip) {
            q->kill();
            _packet = 0;
            return false;
        }

        // Keep the old length for incremental IP checksum calculation
        uint16_t len_old = ip->ip_len;

        // Update the IP header
        ip->ip_len = htons(q->length() - offset);
        // and don't forget the checksum
        if (len_old != ip->ip_len) {
            click_update_in_cksum(&ip->ip_sum, len_old, ip->ip_len);
        }

        p = q;

        SET_EXTRA_LENGTH_ANNO(p, len - desired_len);
    } else {
        SET_EXTRA_LENGTH_ANNO(p, len - caplen);
    }

    _ff.shift_pos(skiplen);

    // Raw IP case; Leave the link layer information out
    if (_linktype == FAKE_DLT_RAW) {
        p->set_network_header(p->data());
    // Regular case; frames contain link layer info
    } else {
        p->set_mac_header(p->data());
    }
    _packet = p;

    return true;
}

inline bool
FromDump::check_timing(Packet *p, Timestamp& now_s, bool& fresh)
{
again:

    now_s = Timestamp::now_steady();
    int64_t elapsed_real = (now_s - _last_real).usecval();

    int64_t elapsed_virt = (p->timestamp_anno() - _timing_offset).usecval();

    if (_current_accel != 100) {
        elapsed_virt = ((double)elapsed_virt * 100.0f) / (double)_current_accel;
    }

    if (elapsed_real < elapsed_virt) {
        if (!fresh) {
            now_s = Timestamp::now_steady();
            fresh = true;
            goto again;
        }
        /*ACCUMULATION NOT NEEDED
         * if (elapsed_real > (INT_MAX / 101)) {
            _last_real = now_s;
            _timing_offset = _timing_offset + Timestamp::make_usec((uint64_t)elapsed_real * (uint64_t)_current_accel / 100));
            goto again;
        }*/
        elapsed_virt -= Timer::adjustment().usecval();

        if (elapsed_real < elapsed_virt) {
            _timer.schedule_at_steady(_last_real + Timestamp::make_usec(elapsed_virt));
            if (output_is_pull(0))
                _notifier.sleep();
        } else {
            if (output_is_push(0))
                _task.fast_reschedule();
        }

	    return false;
    }

    if (_fnt_expr && (elapsed_real - _last_check > 1000)) {
        float x = (float)(now_s - _starttime).msecval() / 1000.0;
        x = _fnt_expr.eval(x);

        if ((unsigned)x != _current_accel) {
            click_chatter("Current accel %f", x);
            _current_accel = x;
            if (_current_accel == 0) {
	            set_active(false);

	           router()->please_stop_driver();
                _current_accel = 1;
            }
           _last_check = 0;
           goto accum;
        } else
            _last_check = elapsed_real;
    }
    return true;
accum:
    _last_real = now_s;
    _timing_offset = p->timestamp_anno();
    return true;
}

void
FromDump::run_timer(Timer *)
{
    if (_active) {
	if (output_is_push(0))
	    _task.reschedule();
	else
	    _notifier.wake();
    }
}

bool
FromDump::run_task(Task *)
{
    Timestamp now_s = Timestamp::now_steady();
    bool fresh = true;
    unsigned n = 0;
    int retry_count = 0;
#if HAVE_BATCH
    BatchBuilder batch;
#endif
  again:
    if (!_active)
	    goto exit;

    if (!_packet && !read_packet(0)) {
        if (_end_h)
            _end_h->call_write(ErrorHandler::default_handler());
        goto exit;
    }
    if (_packet && _timing) {
        if (!check_timing(_packet, now_s, fresh)) {
		    goto exit;
        }
    }
    if (_packet && _force_ip && !fake_pcap_force_ip(_packet, _linktype)) {
#if HAVE_BATCH
        if (in_batch_mode) {
            checked_output_push_batch(1, PacketBatch::make_from_packet(_packet));
        } else
#endif
            checked_output_push(1, _packet);
        _packet = 0;
    }

    if (!_packet && ++retry_count < 16) {
        fresh = false;
	    goto again;
    }

    _task.fast_reschedule();
    if (_packet) {
        #if HAVE_BATCH
            if (in_batch_mode) {
                batch.append(_packet);

            } else
        #endif
	            output(0).push(_packet);
        _count++;
        _packet = 0;
        if (++n < _burst)
            goto again;
        goto exit;
    } else
        goto exit;

exit:
#if HAVE_BATCH
    if (in_batch_mode && batch.count)
        output(0).push_batch(batch.finish());
#endif
    return n > 0;
}

#if HAVE_BATCH
PacketBatch *
FromDump::pull_batch(int port, unsigned max) {
    PacketBatch* batch = 0;
    MAKE_BATCH(FromDump::pull(port),batch,max);
    return batch;
}
#endif

Packet *
FromDump::pull(int)
{
    if (!_active) {
	    _notifier.sleep();
	    return 0;
    }

    bool more = true;
    if (!_packet)
	    more = read_packet(0);

    if (_packet && _timing) {
        //TODO : take advantage of batching to avoid recomputing this
        Timestamp now_s = Timestamp::now_steady();
        bool fresh = true;
        if (!check_timing(_packet, now_s, fresh)) {
	        return 0;
        }
    }

    if (_packet && _force_ip && !fake_pcap_force_ip(_packet, _linktype)) {
	checked_output_push(1, _packet);
	    _packet = 0;
    }

    // notify presence/absence of more packets
    _notifier.set_active(more, true);
    if (!more && _end_h)
	_end_h->call_write(ErrorHandler::default_handler());

    if (Packet *p = _packet) {
	_count++;
	_packet = 0;
	return p;
    } else {
	return 0;
    }
}

enum {
    H_SAMPLING_PROB, H_ACTIVE, H_ENCAP, H_STOP, H_PACKET_FILEPOS,
    H_EXTEND_INTERVAL, H_COUNT, H_RESET_COUNTS, H_RESET_TIMING
};

String
FromDump::read_handler(Element *e, void *thunk)
{
    FromDump *fd = static_cast<FromDump *>(e);
    switch ((intptr_t)thunk) {
    case H_SAMPLING_PROB:
	return cp_unparse_real2(fd->_sampling_prob, SAMPLING_SHIFT);
    case H_ENCAP:
	return String(fake_pcap_unparse_dlt(fd->_linktype));
    default:
	return "<error>";
    }
}

int
FromDump::write_handler(const String &s_in, Element *e, void *thunk, ErrorHandler *errh)
{
    FromDump *fd = static_cast<FromDump *>(e);
    String s = cp_uncomment(s_in);
    switch ((intptr_t)thunk) {
      case H_ACTIVE: {
	  bool active;
	  if (BoolArg().parse(s, active)) {
	      fd->set_active(active);
	      return 0;
	  } else
	      return errh->error("type mismatch");
      }
      case H_STOP:
	fd->set_active(false);
	fd->router()->please_stop_driver();
	return 0;
      case H_EXTEND_INTERVAL: {
	  Timestamp ts;
	  if (cp_time(s, &ts)) {
	      fd->_last_time += ts;
	      if (fd->_end_h)
		  fd->_have_last_time = true, fd->set_active(true);
	      return 0;
	  } else
	      return errh->error("'extend_interval' takes a time interval");
      }
      case H_RESET_COUNTS:
	fd->_count = 0;
	return 0;
      case H_RESET_TIMING:
	fd->_first_time_relative = false;
	fd->_last_time_relative = fd->_last_time_interval = false;
	fd->_have_any_times = false;
	return 0;
      default:
	return -EINVAL;
    }
}

void
FromDump::add_handlers()
{
    _ff.add_handlers(this, true);
    add_read_handler("sampling_prob", read_handler, H_SAMPLING_PROB);
    add_data_handlers("active", Handler::OP_READ | Handler::CHECKBOX, &_active);
    add_write_handler("active", write_handler, H_ACTIVE);
    add_read_handler("encap", read_handler, H_ENCAP);
    add_write_handler("stop", write_handler, H_STOP, Handler::BUTTON);
    add_data_handlers("packet_filepos", Handler::OP_READ, &_packet_filepos);
    add_write_handler("extend_interval", write_handler, H_EXTEND_INTERVAL);
    add_data_handlers("count", Handler::OP_READ, &_count);
    add_write_handler("reset_counts", write_handler, H_RESET_COUNTS, Handler::BUTTON);
    add_write_handler("reset_timing", write_handler, H_RESET_TIMING, Handler::BUTTON);
    if (output_is_push(0))
	add_task_handlers(&_task);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel|ns FakePcap)
EXPORT_ELEMENT(FromDump)
