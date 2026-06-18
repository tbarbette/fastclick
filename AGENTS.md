# FastClick Agent Instructions

FastClick is a high-performance packet processing framework extending Click Modular Router with DPDK support, batching, flow management, and performance optimizations.

## Quick Reference

### Build Commands

**Basic DPDK build (recommended):**
```bash
./configure CFLAGS="-O3" CXXFLAGS="-std=c++11 -O3" \
  --enable-dpdk --enable-intel-cpu --disable-dynamic-linking \
  --enable-bound-port-transfer --enable-flow \
  --disable-task-stats --disable-cpu-load
make
```

**FastClick "Light" (maximum performance):**
```bash
./configure CFLAGS="-O3" CXXFLAGS="-std=c++11 -O3" \
  --enable-dpdk --enable-intel-cpu --disable-dynamic-linking \
  --enable-bound-port-transfer --enable-flow \
  --disable-task-stats --disable-cpu-load \
  --enable-dpdk-packet --disable-clone --disable-dpdk-softqueue
make
```

**User-level only (no DPDK):**
```bash
./configure --enable-userlevel
make
```

### Run Configuration

```bash
# User-level
./userlevel/click conf/myconfig.click

# Kernel module (deprecated for most uses)
sudo click-install conf/myconfig.click
```

### Test

```bash
cd test
./clicktest standard/*.clicktest
./clicktest -j4 standard/*.clicktest  # parallel execution
```

## Project Structure

- **`elements/`** - Element implementations organized by category
  - `standard/` - Core elements (Queue, Discard, Tee, etc.)
  - `ip/` - IP processing (IPRateMonitor, IPFragmenter, etc.)
  - `flow/` - Flow-based elements (MiddleClick)
  - `userlevel/` - DPDK elements (FromDPDKDevice, ToDPDKDevice)
  - `tcpudp/` - TCP/UDP processing
- **`include/click/`** - Public API headers
  - `element.hh` - Base Element class
  - `packet.hh` - Packet abstraction
  - `batchelement.hh` - Batching support
  - `flow/` - Flow subsystem headers
- **`lib/`** - Core implementation (router, packet, element lifecycle)
- **`userlevel/`** - User-space driver
- **`linuxmodule/`** - Linux kernel module (deprecated, use user-level)
- **`conf/`** - Example configurations
- **`test/`** - Test infrastructure and test files

## Creating Elements

### Element Structure

Every element needs a header (`.hh`) and implementation (`.cc`):

**Header (`elements/mypackage/myelement.hh`):**
```cpp
#ifndef CLICK_MYELEMENT_HH
#define CLICK_MYELEMENT_HH
#include <click/batchelement.hh>
CLICK_DECLS

class MyElement : public BatchElement {
public:
    MyElement() CLICK_COLD;
    
    // Required metadata methods
    const char *class_name() const override { return "MyElement"; }
    const char *port_count() const override { return "1/1"; }  // inputs/outputs
    const char *processing() const override { return PUSH; }
    
    // Lifecycle
    int configure(Vector<String> &conf, ErrorHandler *errh) override;
    int initialize(ErrorHandler *errh) override;
    void cleanup(CleanupStage stage) override;
    
    // Processing (implement at least one)
    void push(int port, Packet *p) override;
    
#if HAVE_BATCH
    void push_batch(int port, PacketBatch *batch) override;
#endif
    
private:
    bool _active;
    uint64_t _count;
};

CLICK_ENDDECLS
#endif
```

**Implementation (`elements/mypackage/myelement.cc`):**
```cpp
#include <click/config.h>
#include "myelement.hh"
#include <click/args.hh>
CLICK_DECLS

MyElement::MyElement() : _active(true), _count(0) { }

int MyElement::configure(Vector<String> &conf, ErrorHandler *errh) {
    return Args(conf, this, errh)
        .read_mp("REQUIRED_ARG", _var)      // mandatory positional
        .read_p("OPTIONAL_ARG", _opt)       // optional positional
        .read("KEYWORD", _keyword)          // keyword argument
        .complete();
}

void MyElement::push(int, Packet *p) {
    _count++;
    // Process packet
    output(0).push(p);
}

#if HAVE_BATCH
void MyElement::push_batch(int, PacketBatch *batch) {
    EXECUTE_FOR_EACH_PACKET(simple_action, batch);
    output_push_batch(0, batch);
}
#endif

CLICK_ENDDECLS
EXPORT_ELEMENT(MyElement)  // Register element
ELEMENT_MT_SAFE(MyElement) // Mark as thread-safe (if applicable)
```

### Key Element Patterns

**Port count format:** `"inputs/outputs"` or `"inputs-max_inputs/outputs-max_outputs"`
- `"1/1"` - exactly 1 input, 1 output
- `"1-/1-"` - at least 1 input/output, unlimited
- `PORTS_1_1` - constant for `"1/1"`

**Processing modes:**
- `PUSH` - push-only processing
- `PULL` - pull-only processing
- `AGNOSTIC` - can work in push or pull
- `PROCESSING_A_AH` - agnostic with additional hints

**Batching macros:**
```cpp
FOR_EACH_PACKET(batch, p) { /* process p */ }
FOR_EACH_PACKET_SAFE(batch, p) { /* can modify batch structure for previous and current packet */ }
EXECUTE_FOR_EACH_PACKET(function, batch) //Function is called for each packet of the batch. The batch can't be modified
EXECUTE_FOR_EACH_PACKET_DROPPABLE(function, batch, on_drop) //Same but if function returns 0, then on_drop is called with the original packet
```

## Coding Conventions

### C++ Standards
- **C++11 minimum** (use `-std=c++11` or `-std=gnu++11`)
- C++14 for PacketMill (`-std=gnu++14`)
- Supports GCC and Clang

### Naming Conventions
- **Classes:** `PascalCase` (e.g., `IPRateMonitor`, `FromDPDKDevice`)
- **Methods:** `snake_case` (e.g., `class_name()`, `push_batch()`)
- **Private members:** `_underscore_prefix` (e.g., `_count`, `_active`)
- **Macros:** `UPPER_CASE` (e.g., `CLICK_DECLS`, `HAVE_BATCH`)

### Click-Specific Idioms

**Header guards:**
```cpp
#ifndef CLICK_ELEMENTNAME_HH
#define CLICK_ELEMENTNAME_HH
// ... code ...
#endif
```

**Namespace declarations:**
```cpp
CLICK_DECLS
// ... declarations ...
CLICK_ENDDECLS
```

**Logging:**
```cpp
click_chatter("Debug message: %d", value);
```

**Error handling:**
```cpp
int MyElement::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (bad_config)
        return errh->error("Invalid configuration");
    errh->warning("Non-fatal warning");
    return 0;  // success
}
```

**Packet memory management:**
```cpp
p->kill();                      // destroy packet
batch->fast_kill();             // batch destroy with atomic counter
batch->fast_kill_nonatomic();   // faster if single-threaded
```

## DPDK Integration

### Key DPDK Elements
- **`FromDPDKDevice`** - Receive packets from DPDK port
- **`ToDPDKDevice`** - Send packets to DPDK port
- **`EnsureDPDKBuffer`** - Convert packets to DPDK buffers
- **`DPDKInfo`** - Query DPDK device information

### DPDK Configuration Example
```click
// Multi-queue RSS (auto-assign threads)
FromDPDKDevice(0)
    -> ... processing ...
    -> ToDPDKDevice(1);
```

### Important DPDK Parameters
- **`PORT`** - DPDK port ID
- **`QUEUE`** - RX/TX queue ID
- **`N_QUEUES`** - Number of queues (-1 = one per thread)
- **`BURST`** - Max packets per batch (default: 32)
- **`PROMISC`** - Promiscuous mode (default: true)
- **`NDESC`** - Number of descriptors per queue

## Flow Support (MiddleClick)

Flow support enables stateful packet processing with automatic flow tracking.

### Key Flow Elements
- **`CTXManager`** / **`FlowIPManager_DPDK`** - Flow classification and state management
- **`FlowIPNAT`** - Stateful NAT
- **`FlowIPLoadBalancer`** - Per-flow load balancing
- **`FlowCounter`** - Per-flow counters
- **`FlowRateLimiter`** - Per-flow rate limiting
- **`TCPReorder`** - TCP stream reassembly

### Flow Configuration Example
```click
FromDPDKDevice(0)
    -> CTXManager(BUILDER 1, CACHESIZE 65536)
    -> FlowIPNAT(SIP 10.0.0.1)
    -> ToDPDKDevice(1);
```

### Enable Flow Support
```bash
./configure --enable-flow --enable-ctx --enable-batch
```

**Note:** `--enable-flow` requires `--enable-batch`

## Performance Features

### Batching
- Enabled with `--enable-batch` (required for flows)
- Use `BatchElement` base class
- Implement `push_batch()` / `pull_batch()` methods
- Reduces per-packet overhead significantly

### RSS++ (Auto-scaling)
- NIC-driven thread scheduler
- Automatically scales active threads based on load
- Element: `DeviceBalancer(METHOD rsspp)`
- Requires `--enable-dpdk` and RSS support

### PacketMill (Binary Specialization)
- Generates optimized binaries by embedding configuration constants
- Enables: `--enable-dpdk-packet --disable-clone --disable-dpdk-softqueue`
- Use `click-devirtualize` tool for specialization

## Common Pitfalls

1. **DPDK not found:** Install dependencies with `./deps.sh` or manually install libelf-dev, libnuma-dev
2. **Flow requires batch:** Always enable `--enable-batch` before `--enable-flow`
3. **DPDK buffer conversion:** Use `EnsureDPDKBuffer` before `ToDPDKDevice` when processing non-DPDK packets
4. **Thread safety:** Mark elements with `ELEMENT_MT_SAFE()` only if truly thread-safe
5. **Batch processing:** Remember to call `batch->fast_kill()` instead of killing packets individually
6. **Configuration parsing:** Always use `Args` class, not manual parsing
7. **Memory leaks:** Always call `p->kill()` or pass packets to output ports
8. **Kernel module:** Prefer user-level with DPDK over kernel module for modern deployments

## Configuration Language

Click configurations are directed graphs where elements are nodes and connections are edges.

**Basic syntax:**
```click
// Element declaration
elem :: ElementName(PARAM value);

// Direct connection
Source -> Sink;

// Named port selection
elem[0] -> output_zero;
elem[1] -> output_one;

// Multi-output (duplicates)
Source -> Tee(2) -> [0]Path1; [1]Path2;
```

**Example configuration:**
```click
// Generator -> Classifier -> Counter -> Discard
InfiniteSource(LENGTH 64, LIMIT 1000000)
    -> c :: Classifier(12/0806, -);  // ARP classifier
    
c[0] -> counter0 :: Counter -> Discard;  // ARP packets
c[1] -> counter1 :: Counter -> Discard;  // Other packets
```

## Documentation

- **Wiki:** https://github.com/tbarbette/fastclick/wiki
- **Original Click Manual:** http://read.cs.ucla.edu/click/
- **Lost of all elements:** https://github.com/tbarbette/fastclick/wiki/Elements
- **Tool documentation:** `doc/*.md` and inline in element source
- **DPDK documentation:** http://doc.dpdk.org/

## Links to Key Documentation
- [README.md](README.md) - Project overview and quick start
- [INSTALL.md](INSTALL.md) - Detailed installation instructions
- [README.middleclick.md](README.middleclick.md) - Flow/session support details
- [README.packetmill.md](README.packetmill.md) - Binary specialization
- [conf/README.md](conf/README.md) - Example configurations
- [High-Speed I/O Wiki](https://github.com/tbarbette/fastclick/wiki/High-speed-I-O) - DPDK and Netmap setup
- [GitHub Discussions](https://github.com/tbarbette/fastclick/discussions) - Community support

---

*This file helps AI coding agents understand FastClick conventions and be productive immediately. For detailed feature documentation, see the linked resources above.*
