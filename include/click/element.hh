// -*- c-basic-offset: 4; related-file-name: "../../lib/element.cc" -*-
#ifndef CLICK_ELEMENT_HH
#define CLICK_ELEMENT_HH
#include <click/glue.hh>
#include <click/vector.hh>
#include <click/string.hh>
#include <click/packet.hh>
#include <click/packetbatch.hh>
#include <click/handler.hh>
#include <click/sync.hh>
#include <functional>

#ifdef HAVE_RAND_ALIGN
#include <random>
#endif

CLICK_DECLS
class Router;
class Master;
class RouterThread;
class Task;
class IdleTask;
class Timer;
class NotifierSignal;
class Element;
class ErrorHandler;
class Bitvector;
class EtherAddress;

class BatchElement;

#define BATCH_MAX_PULL 256

/** @file <click/element.hh>
 * @brief Click's Element class.
 */

#ifndef CLICK_ELEMENT_DEPRECATED
# define CLICK_ELEMENT_DEPRECATED CLICK_DEPRECATED
#endif

class Element { public:

    Element();
    virtual ~Element();
    static int nelements_allocated;

    // RUNTIME
    virtual void push(int port, Packet *p);
    virtual Packet *pull(int port) CLICK_WARN_UNUSED_RESULT;
    virtual Packet *simple_action(Packet *p);
#if HAVE_BATCH
    virtual void push_batch(int port, PacketBatch *p);
    virtual PacketBatch* pull_batch(int port,unsigned max) CLICK_WARN_UNUSED_RESULT;
#endif

    virtual bool run_task(Task *task);  // return true iff did useful work
    virtual void run_timer(Timer *timer);
    virtual bool run_idle_task(IdleTask *task);
#if CLICK_USERLEVEL
    enum { SELECT_READ = 1, SELECT_WRITE = 2 };
    virtual void selected(int fd, int mask);
    virtual void selected(int fd);
#endif

#ifdef HAVE_RAND_ALIGN

   static int nalloc;
   static std::mt19937 generator;
// Overloading CLass specific new operator
  inline static void* operator new(size_t sz)
  {
      char * env = getenv("CLICK_ELEM_RAND_MAX");
      int max = 0;
      if (env)
          max = atoi(env);
      max = max / alignof(Element);
      int of;
      if (max > 0) {
          int rand = generator() / (generator.max() / max);
          of = (rand) * alignof(Element);
      } else {
          of = 0;
      }
    void* m = aligned_alloc(alignof(Element), sz + of);
    //click_chatter("EALLOC %d OF %d AL %d", sz, of, alignof(Element) );

    //click_chatter("RESULT-EL%d %d", nalloc++, of );

    // Generate an interrupt
// std::raise(SIGINT);
    return ((unsigned char*)m) + of;
  }
  static void* operator new(size_t sz, void* p)
  {
    return p;
  }
  // Overloading CLass specific delete operator
  static void operator delete(void* m)
  {
    //Let's leak
  }
#endif

    inline bool is_fullpush() const;
    enum batch_mode {BATCH_MODE_NO, BATCH_MODE_IFPOSSIBLE, BATCH_MODE_NEEDED, BATCH_MODE_YES};

    inline void checked_output_push(int port, Packet *p) const;
    inline Packet* checked_input_pull(int port) const;

    // ELEMENT CHARACTERISTICS
    virtual const char *class_name() const = 0;

    virtual const char *port_count() const;
    static const char PORTS_0_0[];
    static const char PORTS_0_1[];
    static const char PORTS_1_0[];
    static const char PORTS_1_1[];
    static const char PORTS_1_1X2[];

    virtual const char *processing() const;
    static const char AGNOSTIC[];
    static const char PUSH[];
    static const char PULL[];
    static const char PUSH_TO_PULL[];
    static const char PULL_TO_PUSH[];
    static const char PROCESSING_A_AH[];

    virtual const char *flow_code() const;
    static const char COMPLETE_FLOW[];

    virtual const char *flags() const;
    int flag_value(int flag) const;

    virtual void *cast(const char *name);
    virtual void *port_cast(bool isoutput, int port, const char *name);

    // CONFIGURATION, INITIALIZATION, AND CLEANUP
    enum ConfigurePhase {
        CONFIGURE_PHASE_FIRST = 0,
        CONFIGURE_PHASE_INFO = 20,
        CONFIGURE_PHASE_PRIVILEGED = 90,
        CONFIGURE_PHASE_DEFAULT = 100,
        CONFIGURE_PHASE_LAST = 2000
    };
    virtual int configure_phase() const;

    virtual int configure(Vector<String> &conf, ErrorHandler *errh);

    virtual void add_handlers();

    virtual int initialize(ErrorHandler *errh);

    virtual void take_state(Element *old_element, ErrorHandler *errh);
    virtual Element *hotswap_element() const;

    enum CleanupStage {
        CLEANUP_NO_ROUTER,
        CLEANUP_BEFORE_CONFIGURE = CLEANUP_NO_ROUTER,
        CLEANUP_CONFIGURE_FAILED,
        CLEANUP_CONFIGURED,
        CLEANUP_INITIALIZE_FAILED,
        CLEANUP_INITIALIZED,
        CLEANUP_THREAD_INITIALZE_FAILED,
        CLEANUP_THREAD_INITIALIZED,
        CLEANUP_ROUTER_INITIALIZED,
        CLEANUP_MANUAL
    };
    virtual void cleanup(CleanupStage stage);

    static inline void static_initialize();
    static inline void static_cleanup();

    // ELEMENT ROUTER CONNECTIONS
    String name() const;
    virtual String declaration() const;

    inline Router *router() const;
    inline int eindex() const;
    inline int eindex(Router *r) const;

    /** @brief Return the element's master. */
    inline Master *master() const;

    inline void attach_router(Router *r, int eindex) {
        assert(!_router);
        _router = r;
        _eindex = eindex;
    }

    // INPUTS AND OUTPUTS
    inline int nports(bool isoutput) const;
    inline int ninputs() const;
    inline int noutputs() const;

    class Port;
    inline const Port &port(bool isoutput, int port) const;
    inline const Port &input(int port) const;
    inline const Port &output(int port) const;

    inline bool port_active(bool isoutput, int port) const;
    inline bool input_is_push(int port) const;
    inline bool input_is_pull(int port) const;
    inline bool output_is_push(int port) const;
    inline bool output_is_pull(int port) const;
    void port_flow(bool isoutput, int port, Bitvector*) const;

    // LIVE RECONFIGURATION
    String configuration() const;

    virtual bool can_live_reconfigure() const;
    virtual int live_reconfigure(Vector<String>&, ErrorHandler*);

    RouterThread *home_thread() const;

    virtual bool get_spawning_threads(Bitvector& b, bool isoutput, int port);

    Bitvector get_pushing_threads();
    Bitvector get_passing_threads(bool forward, int port, Element* origin, bool &_is_fullpush, int level = 0, bool touching = false);
    Bitvector get_passing_threads(Element* origin, int level = 0, bool touching = false);
    Bitvector get_passing_threads(bool touching = false);

    Bitvector get_spawning_threads();

    int home_thread_id() const;
    virtual bool is_mt_safe();
    virtual bool do_mt_safe_check(ErrorHandler*);
    void add_remote_element(Element* e);

    // This feature should be re-designed. I'm not sure a new stage is a good idea. The beginning of future/promise patterns is more interesting.
    enum ThreadReconfigurationStage {
        THREAD_INITIALIZE,
        THREAD_RECONFIGURE_UP_PRE,
        THREAD_RECONFIGURE_UP_POST,
        THREAD_RECONFIGURE_DOWN_PRE,
        THREAD_RECONFIGURE_DOWN_POST,
    };

    virtual int thread_configure(ThreadReconfigurationStage stage, ErrorHandler* errh, Bitvector threads);
    void trigger_thread_reconfiguration(bool is_up, std::function<void()> ready, Bitvector threads);

    //Deprecated name, implement get_spawning_threads
    virtual bool get_runnable_threads(Bitvector&) final = delete;
    virtual bool get_spawning_threads(Bitvector&) final = delete;

#if CLICK_USERLEVEL
    // SELECT
    int add_select(int fd, int mask);
    int remove_select(int fd, int mask);
#endif

    // HANDLERS
    void add_read_handler(const String &name, ReadHandlerCallback read_callback, const void *user_data = 0, uint32_t flags = 0);
    void add_read_handler(const String &name, ReadHandlerCallback read_callback, int user_data, uint32_t flags = 0);
    void add_read_handler(const char *name, ReadHandlerCallback read_callback, int user_data = 0, uint32_t flags = 0);
    void add_write_handler(const String &name, WriteHandlerCallback write_callback, const void *user_data = 0, uint32_t flags = 0);
    void add_write_handler(const String &name, WriteHandlerCallback write_callback, int user_data, uint32_t flags = 0);
    void add_write_handler(const char *name, WriteHandlerCallback write_callback, int user_data = 0, uint32_t flags = 0);
    void set_handler(const String &name, int flags, HandlerCallback callback, const void *read_user_data = 0, const void *write_user_data = 0);
    void set_handler(const String &name, int flags, HandlerCallback callback, int read_user_data, int write_user_data = 0);
    void set_handler(const char *name, int flags, HandlerCallback callback, int read_user_data = 0, int write_user_data = 0);
    int set_handler_flags(const String &name, int set_flags, int clear_flags = 0);
    enum { TASKHANDLER_WRITE_SCHEDULED = 1,
           TASKHANDLER_WRITE_TICKETS = 2,
           TASKHANDLER_WRITE_HOME_THREAD = 4,
           TASKHANDLER_WRITE_ALL = 7,
           TASKHANDLER_DEFAULT = 6 };
    void add_task_handlers(Task *task, NotifierSignal *signal, int flags, const String &prefix = String());
    inline void add_task_handlers(Task *task, NotifierSignal *signal, const String &prefix = String()) {
        add_task_handlers(task, signal, TASKHANDLER_DEFAULT, prefix);
    }
    inline void add_task_handlers(Task *task, const String &prefix = String()) {
        add_task_handlers(task, 0, TASKHANDLER_DEFAULT, prefix);
    }

    void add_data_handlers(const char *name, int flags, uint8_t *data);
    void add_data_handlers(const char *name, int flags, bool *data);
    void add_data_handlers(const char *name, int flags, uint16_t *data);
    void add_data_handlers(const char *name, int flags, int *data);
    void add_data_handlers(const char *name, int flags, unsigned *data);
    void add_data_handlers(const char *name, int flags, atomic_uint32_t *data);
    void add_data_handlers(const char *name, int flags, long *data);
    void add_data_handlers(const char *name, int flags, unsigned long *data);
#if HAVE_LONG_LONG
    void add_data_handlers(const char *name, int flags, long long *data);
    void add_data_handlers(const char *name, int flags, unsigned long long *data);
#endif
    void add_net_order_data_handlers(const char *name, int flags, uint16_t *data);
    void add_net_order_data_handlers(const char *name, int flags, uint32_t *data);
#if HAVE_FLOAT_TYPES
    void add_data_handlers(const char *name, int flags, double *data);
#endif
    void add_data_handlers(const char *name, int flags, String *data);
    void add_data_handlers(const char *name, int flags, IPAddress *data);
    void add_data_handlers(const char *name, int flags, EtherAddress *data);
    void add_data_handlers(const char *name, int flags, Timestamp *data, bool is_interval = false);

    static String read_positional_handler(Element*, void*);
    static String read_keyword_handler(Element*, void*);
    static int reconfigure_positional_handler(const String&, Element*, void*, ErrorHandler*);
    static int reconfigure_keyword_handler(const String&, Element*, void*, ErrorHandler*);

    virtual int llrpc(unsigned command, void* arg);
    int local_llrpc(unsigned command, void* arg);

    class Port { public:

        inline bool active() const;
        inline Element* element() const;
        inline int port() const;

        inline void push(Packet* p) const;
        inline Packet* pull() const;
#if HAVE_BATCH
        inline void push_batch(PacketBatch* p) const;
        inline PacketBatch* pull_batch(unsigned max) const;

        inline void start_batch();
        inline void end_batch();
#endif
#if HAVE_FLOW_DYNAMIC
        inline void set_unstack(bool unstack) {
            _unstack = unstack;
        }
        inline bool unstack() const {
            return _unstack;
        }
#endif

#if CLICK_STATS >= 1
        unsigned npackets() const       { return _packets; }
#endif

        inline void assign_peer(bool isoutput, Element *e, int port, bool unstack=false);

      private:

        Element* _e;
        int _port;
    #ifdef HAVE_AUTO_BATCH
        per_thread<PacketBatch*> current_batch;
	#endif
#if HAVE_FLOW_DYNAMIC
        bool _unstack;
#endif

#if HAVE_BOUND_PORT_TRANSFER
        union {
            void (*push)(Element *e, int port, Packet *p);
            Packet *(*pull)(Element *e, int port);
        } _bound;
        union {
#if HAVE_BATCH
            void (*push_batch)(Element *e, int port, PacketBatch *p);
            PacketBatch* (*pull_batch)(Element *e, int port, unsigned max);
#endif
        } _bound_batch;
#endif

#if CLICK_STATS >= 1
        mutable unsigned _packets;      // How many packets have we moved?
#endif
#if CLICK_STATS >= 2
        Element* _owner;                // Whose input or output are we?
#endif

        inline Port();
        inline void assign_owner(bool isoutput, Element *owner, Element *e, int port, bool unstack=false);

        friend class Element;
        friend class BatchElement;

    };

    // DEPRECATED
    /** @cond never */
    String id() const CLICK_DEPRECATED;
    String landmark() const CLICK_DEPRECATED;
    /** @endcond never */

  protected:
    enum batch_mode in_batch_mode;
    bool receives_batch;

  private:

    enum { INLINE_PORTS = 4 };

    Port* _ports[2];
    Port _inline_ports[INLINE_PORTS];

    int _nports[2];

    Router* _router;
    int _eindex;

    Vector<Element*> _remote_elements;
#if HAVE_FULLPUSH_NONATOMIC
    bool _is_fullpush;
#endif

#if CLICK_STATS >= 2
    // STATISTICS
    unsigned _xfer_calls;       // Push and pull calls into this element.
    click_cycles_t _xfer_own_cycles;    // Cycles spent in self from push and pull.
    click_cycles_t _child_cycles;       // Cycles spent in children.

    unsigned _task_calls;       // Calls to tasks owned by this element.
    click_cycles_t _task_own_cycles;    // Cycles spent in self from tasks.

    unsigned _timer_calls;      // Calls to timers owned by this element.
    click_cycles_t _timer_own_cycles;   // Cycles spent in self from timers.

    inline void reset_cycles() {
        _xfer_calls = _task_calls = _timer_calls = 0;
        _xfer_own_cycles = _task_own_cycles = _timer_own_cycles = _child_cycles = 0;
    }
    static String read_cycles_handler(Element *, void *);
    static int write_cycles_handler(const String &, Element *, void *, ErrorHandler *);
#endif

    Element(const Element &);
    Element &operator=(const Element &);

    // METHODS USED BY ROUTER
    int set_nports(int, int);
    int notify_nports(int, int, ErrorHandler *);
    enum Processing { VAGNOSTIC, VPUSH, VPULL };
    static int next_processing_code(const char*& p, ErrorHandler* errh);
    void processing_vector(int* input_codes, int* output_codes, ErrorHandler*) const;

    void initialize_ports(const int* input_codes, const int* output_codes);
    int connect_port(bool isoutput, int port, Element*, int);

    static String read_handlers_handler(Element *e, void *user_data);
    void add_default_handlers(bool writable_config);
    inline void add_data_handlers(const char *name, int flags, HandlerCallback callback, void *data);

    friend class BatchElement;
    friend class Router;
#if CLICK_STATS >= 2
    friend class Task;
    friend class Master;
    friend class TimerSet;
# if CLICK_USERLEVEL
    friend class SelectSet;
# endif
#endif

};


/** @brief Initialize static data for this element class.
 *
 * Place initialization code for an element class's shared global state in the
 * static_initialize() static member function.  (For example, the IPFilter
 * element class uses static_initialize() to set up various parsing tables.)
 * Click drivers will call this function when the element code is loaded,
 * before any elements of the class are created.
 *
 * static_initialize functions are called in an arbitrary and unpredictable
 * order (not, for example, the configure_phase() order).  Element authors are
 * responsible for handling static initialization dependencies.
 *
 * For Click to find a static_initialize declaration, it must appear inside
 * the element class's class declaration on its own line and have the
 * following prototype:
 *
 * @code
 * static void static_initialize();
 * @endcode
 *
 * It must also have public accessibility.
 *
 * @note In most cases you should also define a static_cleanup() function to
 * clean up state initialized by static_initialize().
 *
 * @sa Element::static_cleanup
 */
inline void
Element::static_initialize()
{
}

/** @brief Clean up static data for this element class.
 *
 * Place cleanup code for an element class's shared global state in the
 * static_cleanup() static member function.  Click drivers will call this
 * function before unloading the element code.
 *
 * static_cleanup functions are called in an arbitrary and unpredictable order
 * (not, for example, the configure_phase() order, and not the reverse of the
 * static_initialize order).  Element authors are responsible for handling
 * static cleanup dependencies.
 *
 * For Click to find a static_cleanup declaration, it must appear inside the
 * element class's class declaration on its own line and have the following
 * prototype:
 *
 * @code
 * static void static_cleanup();
 * @endcode
 *
 * It must also have public accessibility.
 *
 * @sa Element::static_initialize
 */
inline void
Element::static_cleanup()
{
}

/** @brief Return the element's router. */
inline Router*
Element::router() const
{
    return _router;
}

/** @brief Return the element's index within its router.
 * @invariant this == router()->element(eindex())
 */
inline int
Element::eindex() const
{
    return _eindex;
}

/** @brief Return the element's index within router @a r.
 *
 * Returns -1 if @a r != router(). */
inline int
Element::eindex(Router* r) const
{
    return (router() == r ? _eindex : -1);
}

/** @brief Return the number of input or output ports.
 * @param isoutput false for input ports, true for output ports */
inline int
Element::nports(bool isoutput) const
{
    return _nports[isoutput];
}

/** @brief Return the number of input ports. */
inline int
Element::ninputs() const
{
    return _nports[0];
}

/** @brief Return the number of output ports. */
inline int
Element::noutputs() const
{
    return _nports[1];
}

/** @brief Return one of the element's ports.
 * @param isoutput false for input ports, true for output ports
 * @param port port number
 *
 * An assertion fails if @a p is out of range. */
inline const Element::Port&
Element::port(bool isoutput, int port) const
{
    assert((unsigned) port < (unsigned) _nports[isoutput]);
    return _ports[isoutput][port];
}

/** @brief Return one of the element's input ports.
 * @param port port number
 *
 * An assertion fails if @a port is out of range.
 *
 * @sa Port, port */
inline const Element::Port&
Element::input(int port) const
{
    return Element::port(false, port);
}

/** @brief Return one of the element's output ports.
 * @param port port number
 *
 * An assertion fails if @a port is out of range.
 *
 * @sa Port, port */
inline const Element::Port&
Element::output(int port) const
{
    return Element::port(true, port);
}

/** @brief Check whether a port is active.
 * @param isoutput false for input ports, true for output ports
 * @param port port number
 *
 * Returns true iff @a port is in range and @a port is active.  Push outputs
 * and pull inputs are active; pull outputs and push inputs are not.
 *
 * @sa Element::Port::active */
inline bool
Element::port_active(bool isoutput, int port) const
{
    return (unsigned) port < (unsigned) nports(isoutput)
        && _ports[isoutput][port].active();
}

/** @brief Check whether output @a port is push.
 *
 * Returns true iff output @a port exists and is push.  @sa port_active */
inline bool
Element::output_is_push(int port) const
{
    return port_active(true, port);
}

/** @brief Check whether output @a port is pull.
 *
 * Returns true iff output @a port exists and is pull. */
inline bool
Element::output_is_pull(int port) const
{
    return (unsigned) port < (unsigned) nports(true)
        && !_ports[1][port].active();
}

/** @brief Check whether input @a port is pull.
 *
 * Returns true iff input @a port exists and is pull.  @sa port_active */
inline bool
Element::input_is_pull(int port) const
{
    return port_active(false, port);
}

/** @brief Check whether input @a port is push.
 *
 * Returns true iff input @a port exists and is push. */
inline bool
Element::input_is_push(int port) const
{
    return (unsigned) port < (unsigned) nports(false)
        && !_ports[0][port].active();
}

#if CLICK_STATS >= 2
# define PORT_ASSIGN(o) _packets = 0; _owner = (o)
#elif CLICK_STATS >= 1
# define PORT_ASSIGN(o) _packets = 0; (void) (o)
#else
# define PORT_ASSIGN(o) (void) (o)
#endif

inline
Element::Port::Port()
    : _e(0), _port(-2)
#if HAVE_FLOW_DYNAMIC
      , _unstack(false)
#endif
{
    PORT_ASSIGN(0);
}
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpmf-conversions"
#endif
inline void
Element::Port::assign_peer(bool isoutput, Element *e, int port, bool unstack)
{
    _e = e;
    _port = port;
#if HAVE_FLOW_DYNAMIC
    _unstack = unstack;
#else
    (void)unstack;
#endif
#ifdef HAVE_AUTO_BATCH
    for (unsigned i = 0; i < current_batch.weight() ; i++)
        current_batch.set_value(i,0);
#endif
    (void) isoutput;
#if HAVE_BOUND_PORT_TRANSFER
    if (e) {
        if (isoutput) {
            void (Element::*pusher)(int, Packet *) = &Element::push;
            _bound.push = (void (*)(Element *, int, Packet *)) (e->*pusher);
# if HAVE_BATCH
            void (Element::*pushbatcher)(int, PacketBatch *) = &Element::push_batch;
            _bound_batch.push_batch = (void (*)(Element *, int, PacketBatch *)) (e->*pushbatcher);
# endif
        } else {
            Packet *(Element::*puller)(int) = &Element::pull;
            _bound.pull = (Packet *(*)(Element *, int)) (e->*puller);
# if HAVE_BATCH
             PacketBatch *(Element::*pullbatcher)(int,unsigned) = &Element::pull_batch;
             _bound_batch.pull_batch = (PacketBatch *(*)(Element *, int, unsigned)) (e->*pullbatcher);
# endif
        }
    }
#endif
}
#ifndef __clang__
#pragma GCC diagnostic pop
#endif
inline void
Element::Port::assign_owner(bool isoutput, Element *owner, Element *e, int port, bool unstack)
{
    PORT_ASSIGN(owner);
    assign_peer(isoutput, e, port, unstack);
}

/** @brief Returns whether this port is active (a push output or a pull input).
 *
 * @sa Element::port_active
 */
inline bool
Element::Port::active() const
{
    return _port >= 0;
}

/** @brief Returns the element connected to this active port.
 *
 * Returns 0 if this port is not active(). */
inline Element*
Element::Port::element() const
{
    return _e;
}

/** @brief Returns the port number of the port connected to this active port.
 *
 * Returns < 0 if this port is not active(). */
inline int
Element::Port::port() const
{
    return _port;
}

/** @brief Push packet @a p over this port.
 *
 * Pushes packet @a p downstream through the router configuration by passing
 * it to the next element's @link Element::push() push() @endlink function.
 * Returns when the rest of the router finishes processing @a p.
 *
 * This port must be an active() push output port.  Usually called from
 * element code like @link Element::output output(i) @endlink .push(p).
 *
 * When element code calls Element::Port::push(@a p), it relinquishes control
 * of packet @a p.  When push() returns, @a p may have been altered or even
 * freed by downstream elements.  Thus, you must not use @a p after pushing it
 * downstream.  To push a copy and keep a copy, see Packet::clone().
 *
 * output(i).push(p) basically behaves like the following code, although it
 * maintains additional statistics depending on how CLICK_STATS is defined:
 *
 * @code
 * output(i).element()->push(output(i).port(), p);
 * @endcode
 */
inline void
Element::Port::push(Packet* p) const
{
    assert(_e && p);
#if HAVE_FLOW_DYNAMIC
    FlowControlBlock* tmp_stack = 0;
    if (_unstack) {
        tmp_stack = fcb_stack;
        fcb_stack->release(1);
        fcb_stack = 0;
    }
#endif
#ifdef HAVE_AUTO_BATCH
    if (likely(_e->in_batch_mode == BATCH_MODE_YES)) {
        if (*current_batch != 0) {
            if (*current_batch == (PacketBatch*)-1) {
                *current_batch = PacketBatch::make_from_packet(p);
            } else {
                (*current_batch)->append_packet(p);
            }
        } else {
            push_batch(PacketBatch::make_from_packet(p));
        }
    } else {
        assert(!_e->receives_batch);
#else
    {
#endif
#if CLICK_STATS >= 1
    ++_packets;
#endif
#if CLICK_STATS >= 2
    ++_e->input(_port)._packets;
    click_cycles_t start_cycles = click_get_cycles(),
        start_child_cycles = _e->_child_cycles;
# if HAVE_BOUND_PORT_TRANSFER
    _bound.push(_e, _port, p);
# else
    _e->push(_port, p);
# endif
    click_cycles_t all_delta = click_get_cycles() - start_cycles,
        own_delta = all_delta - (_e->_child_cycles - start_child_cycles);
    _e->_xfer_calls += 1;
    _e->_xfer_own_cycles += own_delta;
    _owner->_child_cycles += all_delta;
#else
# if HAVE_BOUND_PORT_TRANSFER
    _bound.push(_e, _port, p);
# else
    _e->push(_port, p);
# endif
    }
#endif
#if HAVE_FLOW_DYNAMIC
    if (_unstack) {
        fcb_stack = tmp_stack;
    }
#endif
}

/** @brief Pull a packet over this port and return it.
 *
 * Pulls a packet from upstream in the router configuration by calling the
 * previous element's @link Element::pull() pull() @endlink function.  When
 * the router finishes processing, returns the result.
 *
 * This port must be an active() pull input port.  Usually called from element
 * code like @link Element::input input(i) @endlink .pull().
 *
 * input(i).pull() basically behaves like the following code, although it
 * maintains additional statistics depending on how CLICK_STATS is defined:
 *
 * @code
 * input(i).element()->pull(input(i).port())
 * @endcode
 */
inline Packet*
Element::Port::pull() const
{
    assert(_e);
#if CLICK_STATS >= 2
    click_cycles_t start_cycles = click_get_cycles(),
        old_child_cycles = _e->_child_cycles;
# if HAVE_BOUND_PORT_TRANSFER
    Packet *p = _bound.pull(_e, _port);
# else
    Packet *p = _e->pull(_port);
# endif
    if (p)
        _e->output(_port)._packets += 1;
    click_cycles_t all_delta = click_get_cycles() - start_cycles,
        own_delta = all_delta - (_e->_child_cycles - old_child_cycles);
    _e->_xfer_calls += 1;
    _e->_xfer_own_cycles += own_delta;
    _owner->_child_cycles += all_delta;
#else
# if HAVE_BOUND_PORT_TRANSFER
    Packet *p = _bound.pull(_e, _port);
# else
    Packet *p = _e->pull(_port);
# endif
#endif
#if CLICK_STATS >= 1
    if (p)
        ++_packets;
#endif
    return p;
}

#if HAVE_BATCH
/**
 * Push a batch through this port
 */
void
Element::Port::push_batch(PacketBatch* batch) const {
#if HAVE_FLOW_DYNAMIC
    FlowControlBlock* tmp_stack = 0;
    if (unlikely(_unstack && fcb_stack)) {
        tmp_stack = fcb_stack;
        fcb_stack->release(batch->count());
        fcb_stack = 0;
    }
#endif

#if BATCH_DEBUG
    click_chatter("Pushing batch of %d packets to %p{element}",batch->count(),_e);
#endif
#if HAVE_BOUND_PORT_TRANSFER
    _bound_batch.push_batch(_e,_port,batch);
#else
    _e->push_batch(_port,batch);
#endif
#if HAVE_FLOW_DYNAMIC
    if (unlikely(_unstack)) {
        fcb_stack = tmp_stack;
    }
#endif
}

#ifdef HAVE_AUTO_BATCH
inline void
Element::Port::start_batch() {
    /**
     * Port mode : start batching if e is BATCH_MODE_YES
     * List mode : start batching (if e is not BATCH_MODE_YES, we would not be calling this)
     * Jump mode : pass the message through all ports if not BATCH_MODE_YES, if it is, set to -1 to start batching
     */
#if HAVE_AUTO_BATCH == AUTO_BATCH_JUMP
# if BATCH_DEBUG
    click_chatter("Passing start batch message in port to %p{element}",_e);
# endif
    if (_e->in_batch_mode == BATCH_MODE_YES)  {
        if (*current_batch == 0)
            current_batch.set((PacketBatch*)-1);
    } else {
        for (int i = 0; i < _e->noutputs(); i++) {
            if (_e->output_is_push(i))
                _e->_ports[1][i].start_batch();
        }
    }
#elif HAVE_AUTO_BATCH == AUTO_BATCH_PORT
# if BATCH_DEBUG
    click_chatter("Starting batch in port to %p{element}",_e);
# endif
    if (_e->in_batch_mode == BATCH_MODE_YES) { //Rebuild for the next element
        if (*current_batch == 0)
            current_batch.set((PacketBatch*)-1);
    }
#elif HAVE_AUTO_BATCH == AUTO_BATCH_LIST
# if BATCH_DEBUG
    click_chatter("Starting batch in port to %p{element}",_e);
# endif
    assert(*current_batch == 0);
    current_batch.set((PacketBatch*)-1);
#else
    #error "Unknown batch auto mode"
#endif
}

inline void
Element::Port::end_batch() {
    PacketBatch* cur = *current_batch;
#if BATCH_DEBUG
    click_chatter("Ending batch in port to %p{element}",_e);
#endif
    /**
     * Auto mode : if 0 bug, if -1 nothing, else push the batch
     * List mode : if 0 bug, if -1 nothing, else push the batch
     * Jump mode : if BATCH_MODE_YES push the batch, else pass the message (and assert cur was 0)
     */
#if HAVE_AUTO_BATCH == AUTO_BATCH_JUMP
    if (_e->in_batch_mode == BATCH_MODE_YES)
#endif
    { //Send the buffered batch to the next element
       if (cur != 0) {
           current_batch.set((PacketBatch*)0);
           if (cur != (PacketBatch*)-1)
           {
               #if BATCH_DEBUG
               assert(cur->find_count() == cur->count());
               #endif
               _e->push_batch(_port,cur);
           }
       }

    }
#if HAVE_AUTO_BATCH == AUTO_BATCH_JUMP
    else { //Pass the message
        assert(cur == 0);
        for (int i = 0; i < _e->noutputs(); i++) {
            if (_e->output_is_push(i))
                _e->_ports[1][i].end_batch();
        }
    }
#endif
};
#endif

PacketBatch*
Element::Port::pull_batch(unsigned max) const {
    PacketBatch* batch = NULL;
#if HAVE_BOUND_PORT_TRANSFER
    batch = _bound_batch.pull_batch(_e,_port, max);
#else
    batch = _e->pull_batch(_port, max);
#endif
    return batch;
}
#endif

/**
 * @brief Tell if the path up to this element is a full push path, always
 * served by the same thread.
 *
 * Hence, it is not only a matter of having
 * only a push path, as some elements like Pipeliner may be push
 * but lead to thread switch.
 *
 * @pre get_passing_threads() have to be called on this element or any downstream element
 *
 * If this element is part of a full push path, it means that packets passing
 *  through will always be handled by the same thread. They may be shared, in
 *  the sense that the usage count could be bigger than one. But then shared
 *  only with the same thread. Therefore non-atomic operations can be involved.
 */
inline bool Element::is_fullpush() const {
#if HAVE_FULLPUSH_NONATOMIC
    return _is_fullpush;
#else
    return false;
#endif
}


/** @brief Push packet @a p to output @a port, or kill it if @a port is out of
 * range.
 *
 * @param port output port number
 * @param p packet to push
 *
 * If @a port is in range (>= 0 and < noutputs()), then push packet @a p
 * forward using output(@a port).push(@a p).  Otherwise, kill @a p with @a p
 * ->kill().
 *
 * @note It is invalid to call checked_output_push() on a pull output @a port.
 */
inline void
Element::checked_output_push(int port, Packet* p) const
{
    if ((unsigned) port < (unsigned) noutputs())
        _ports[1][port].push(p);
    else
        p->kill();
}

/** @brief Pull a packet from input @a port, or return 0 if @a port is out of
 * range.
 *
 * @param port input port number
 *
 * If @a port is in range (>= 0 and < ninputs()), then return the result
 * of input(@a port).pull().  Otherwise, return null.
 *
 * @note It is invalid to call checked_input_pull() on a push input @a port.
 */
inline Packet*
Element::checked_input_pull(int port) const
{
    if ((unsigned) port < (unsigned) ninputs())
        return _ports[0][port].pull();
    else
        return 0;
}

#undef PORT_ASSIGN
CLICK_ENDDECLS
#endif
