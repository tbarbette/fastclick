#ifndef CLICK_DDIOTune_HH
#define CLICK_DDIOTune_HH
#include <click/element.hh>
#include <rte_pci.h>
extern "C"{
#include<pci/pci.h>
}

CLICK_DECLS

/*
=title DDIOTune

=c 

DDIOTune(N_WAYS, DEV [,PRINT])

=s test

Tune the DDIO capacity based on the input number of ways.

=d 

Configure IIO_LLC_WAYS register in Intel processors. In addition, this element can disable DDIO for a specific NIC.

Arguments:

=item N_WAYS

Integer. Number of DDIO ways to use. The default is 2. Passing 0 disables DDIO.

=item DEV

String. PCI device address. For instace, it can be 0000:17:00.0

=item PRINT

Boolean. Print cache hierarhcy information and PCIe root info. The deault is false.

=e 

  DDIOTune(N_WAYS 4, DEV 0000:17:00.0, PRINT true) 
*/

/*
 * CPUID information:
 *
 * INPUT EAX = 04H: Returns Deterministic Cache Parameters for Each Level
 * When CPUID executes with EAX set to 04H and ECX contains an index value,
 * the processor returns encoded data that describe a set of deterministic 
 * cache parameters (for the cache level associated with the input in ECX). 
 * Valid index values start from 0.
 *
 *  = (Ways + 1) * (Partitions + 1) * (Line_Size + 1) * (Sets + 1)
 *  = (EBX[31:22] + 1) * (EBX[21:12] + 1) * (EBX[11:0] + 1) * (ECX + 1)
 *  
 *  Source:Intel 64 and IA-32 Architectures - Software Developer's Manual 
 *  Volume 2 (2A, 2B, 2C & 2D):Instruction Set Reference, A-Z
 *  p. Vol. 2A 3-213
 *  Link: https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf
 */

#define CACHE_LEVELS 4
#define WAY_MASK 0xFFC00000
#define WAY_SHIFT 22
#define PARTITION_MASK 0x3FF000
#define PARTITION_SHIFT 12
#define LINE_SIZE_MASK 0xFFF
#define LINE_SIZE_SHIFT 0

/*
 * PCIe Root Complex Information for Disabling DDIO 
 */

#define PCI_VENDOR_ID_INTEL 0x8086
#define PERFCTRLSTS_0   0x180
#define use_allocating_flow_wr_MASK 0x80
#define nosnoopopwren_MASK  0x8

/* DDIO register info */
#define IIO_LLC_WAYS_REGISTER 0xC8B


class DDIOTune : public Element { 
public:

    DDIOTune() CLICK_COLD;
    ~DDIOTune() CLICK_COLD;

    const char *class_name() const      { return "DDIOTune"; }
    const char *port_count() const      { return PORTS_0_0; }

    int configure_phase() const { return CONFIGURE_PHASE_FIRST; }

    int configure(Vector<String>&, ErrorHandler*) override;

    //TODO: One instance per pci dev
    //static DDIOTune *instance;
private:
    int _w;
    int ddio_state;
    uint8_t _pci_dev;
    struct pci_dev *_dev;
    int max_w;
    bool _print;
    bool _ddio_state;
    struct pci_access *pacc;
    int _numa_node;
    int _msr_cpu;
    
    uint64_t IIO_LLC_WAYS_VALUE, IIO_LLC_WAYS_VALUE_MIN, IIO_LLC_WAYS_VALUE_MAX, IIO_LLC_WAYS_VALUE_NEW;
    uint32_t eax, ebx, ecx, edx;
    uint32_t *ways, *partitions, *linesize, *sets, *size;

    int32_t rdmsr(int msr, uint64_t *val, int cpu);
    int32_t wrmsr(int msr, uint64_t *val, int cpu);
    void print_cache_info();
    void init_pci_access(void);
    struct pci_dev* find_ddio_device(uint8_t nic_bus);
    int ddio_status(uint8_t nic_bus);
    void ddio_enable(uint8_t nic_bus);
    void ddio_disable(uint8_t nic_bus);
    void print_dev_info(struct pci_dev *dev);
};

CLICK_ENDDECLS
#endif
