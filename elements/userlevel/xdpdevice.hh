#pragma once

#include <click/config.h>
#include <click/element.hh>
#include <click/task.hh>

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#define barrier() __asm__ __volatile__("": : :"memory")
#define u_smp_wmb() barrier()
#define u_smp_rmb() barrier()

#define NUM_FRAMES 131072
#define NUM_DESCS 1024
#define FRAME_SIZE 2048
#define FRAME_SHIFT 11
#define FRAME_HEADROOM 0
#define FRAME_TAILROOM 0
#define FQ_NUM_DESCS 1024
#define CQ_NUM_DESCS 1024
#define BATCH_SIZE 16

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

struct xdp_uqueue {
	u32 cached_prod;
	u32 cached_cons;
	u32 mask;
	u32 size;
	u32 *producer{nullptr};
	u32 *consumer{nullptr};
	struct xdp_desc *ring;
	void *map;
};

struct xdpsock {
	struct xdp_uqueue rx;
	struct xdp_uqueue tx;
	int sfd;
	struct xdp_umem *umem;
	u32 outstanding_tx;
	unsigned long rx_npkts;
	unsigned long tx_npkts;
	unsigned long prev_rx_npkts;
	unsigned long prev_tx_npkts;
};

struct xdp_umem_uqueue {
	u32 cached_prod;
	u32 cached_cons;
	u32 mask;
	u32 size;
	u32 *producer{nullptr};
	u32 *consumer{nullptr};
	u64 *ring;
	void *map;
};

struct xdp_umem {
	char *frames;
	struct xdp_umem_uqueue fq;
	struct xdp_umem_uqueue cq;
	int fd;
};

CLICK_DECLS

class XDPDevice : public Element {

  public:

    XDPDevice() CLICK_COLD = default;
    ~XDPDevice() CLICK_COLD = default;

    int configure(Vector<String>&, ErrorHandler*) override CLICK_COLD;
    int initialize(ErrorHandler*) override CLICK_COLD;

    const char *class_name() const override final { return "XDPDevice"; }
    const char *port_count() const override final { return PORTS_1_1; }
    const char *processing() const override final { return PULL_TO_PUSH; }

    bool run_task(Task *t) override final;
    //void push(int port, Packet *p) override final;
    void push();
    void pull();

  private:
    Task *_t{nullptr};

    String _dev;
    String _mode;
    String _prog;

    struct bpf_map *_xsk_map,
                   *_qidconf_map;
    xdpsock *_xsk;

    int _xsk_map_fd,
        _qidconf_map_fd;

    u32 _ifx_index{0};
    u16 _flags{0};
    u16 _bind_flags{0};
    int _sfd;

    void set_rlimit(ErrorHandler*);
    void init_bpf(ErrorHandler*);
    void init_xsk(ErrorHandler*);

    int xq_deq(struct xdp_uqueue *uq, struct xdp_desc *descs, int ndescs);
    void *xq_get_data(struct xdpsock *xsk, u64 addr);
    void hex_dump(void *pkt, size_t length, u64 addr);
    int umem_fill_to_kernel_ex(struct xdp_umem_uqueue *fq, struct xdp_desc *d, size_t nb);
    u32 xq_nb_free(struct xdp_uqueue *q, u32 ndescs);
    void kick_tx(int fd);
    size_t umem_complete_from_kernel( struct xdp_umem_uqueue *cq, u64 *d, size_t nb);
    u32 umem_nb_avail(struct xdp_umem_uqueue *q, u32 nb);
    int umem_fill_to_kernel(struct xdp_umem_uqueue *fq, u64 *d, size_t nb);
    u32 xq_nb_avail(struct xdp_uqueue *q, u32 nb);
    u32 umem_nb_free(struct xdp_umem_uqueue *q, u32 nb);
    struct xdp_umem *umem_config(int sfd);

};
