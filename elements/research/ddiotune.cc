/*
 * DDIOTune.{cc,hh} --
 * Alireza Farshin
 *
 * Copyright (c) 2020 KTH Royal Institute of Technology
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
#include <click/args.hh>
#include <click/error.hh>
#include <cpuid.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cmath>
#include "ddiotune.hh"
#include <rte_lcore.h>

CLICK_DECLS

/* Read from an MSR register */
int DDIOTune::rdmsr(int msr, uint64_t *val, int cpu) {
  int fd;
  char msr_file[50];
  sprintf(msr_file, "/dev/cpu/%d/msr", cpu);

  fd = open(msr_file, O_RDONLY);
  if (fd < 0)
    return fd;

  int ret = pread(fd, val, sizeof(uint64_t), msr);

  close(fd);

  return ret;
}

/* Write to an MSR register */
int DDIOTune::wrmsr(int msr, uint64_t *val, int cpu) {
  int fd;
  char msr_file[50];
  sprintf(msr_file, "/dev/cpu/%d/msr", cpu);

  fd = open(msr_file, O_WRONLY);
  if (fd < 0)
    return fd;

  int ret = pwrite(fd, val, sizeof(uint64_t), msr);

  close(fd);

  return ret;
}

/* Initialize PCI access data structure */
int DDIOTune::init_pci_access() {
  pacc = pci_alloc(); /* Get the pci_access structure */
  if (pacc == NULL)
    return -1;
  pci_init(pacc);     /* Initialize the PCI library */
  pci_scan_bus(pacc); /* We want to get the list of devices */
  return 0;
}

/* Find the responsible PCI root for the input NIC */
struct pci_dev *DDIOTune::find_ddio_device(uint8_t nic_bus) {
  struct pci_dev *dev;
  for (dev = pacc->devices; dev; dev = dev->next) {
    pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_NUMA_NODE |
                           PCI_FILL_PHYS_SLOT);
    /*
     * Find the proper PCIe root based on the nic device
     * For instance, if the NIC is located on 0000:17:00.0 (i.e., BDF)
     * 0x17 is the nic_bus (B)
     * 0x00 is the nic_device (D)
     * 0x0	is the nic_function (F)
     */
    if (pci_read_byte(dev, PCI_SUBORDINATE_BUS) == nic_bus) {
      return dev;
    }
  }
  return NULL;
}

/* Print cache information */
void DDIOTune::print_cache_info() {
  click_chatter("Cache Hierarchy Info");
  click_chatter("====================");
  for (int i = 0; i < CACHE_LEVELS; i++) {
    click_chatter("Level %d:", i);
    click_chatter("#Ways: %d", ways[i]);
    click_chatter("#Partitions: %" PRIu32, partitions[i]);
    click_chatter("Cache Line Size: %d", linesize[i]);
    click_chatter("#Sets: %d", sets[i]);
    click_chatter("Size: %d B", size[i]);
    click_chatter("====================");
  }
}

/* Check DDIO status (i.e., enabled or disabled) for the input NIC */
int DDIOTune::ddio_status() {
  /*
   * perfctrlsts_0
   * bit 3: NoSnoopOpWrEn -> Should be 1b
   * bit 7: Use_Allocating_Flow_Wr -> Should be 0b
   * Check p. 68 of Intel® Xeon® Processor Scalable Family
   * Datasheet, Volume Two: Registers
   * May 2019
   * link:
   * https://www.intel.com/content/www/us/en/processors/xeon/scalable/xeon-scalable-datasheet-vol-2.html
   * Haswell link:
   * https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/xeon-e5-v2-datasheet-vol-2.pdf
   */
  uint32_t val = pci_read_long(_dev, PERFCTRLSTS_0);
  click_chatter("perfctrlsts_0 val: 0x%" PRIx32 "\n", val);
  click_chatter("NoSnoopOpWrEn val: 0x%" PRIx32 "\n", val & nosnoopopwren_MASK);
  click_chatter("Use_Allocating_Flow_Wr val: 0x%" PRIx32 "\n",
                val & use_allocating_flow_wr_MASK);
  if (val & use_allocating_flow_wr_MASK)
    return 1;
  else
    return 0;
}

/* Enable DDIO for the input NIC */
void DDIOTune::ddio_enable() {

  if (!ddio_status()) {
    uint32_t val = pci_read_long(_dev, PERFCTRLSTS_0);
    pci_write_long(_dev, PERFCTRLSTS_0, val | use_allocating_flow_wr_MASK);
    click_chatter("DDIO is enabled!");
  } else {
    click_chatter("DDIO was already enabled!");
  }
}

/* Disable DDIO for the input NIC */
void DDIOTune::ddio_disable() {

  if (ddio_status()) {
    uint32_t val = pci_read_long(_dev, PERFCTRLSTS_0);
    pci_write_long(_dev, PERFCTRLSTS_0, val & (~use_allocating_flow_wr_MASK));
    click_chatter("DDIO is disabled!");
  } else {
    click_chatter("DDIO was already disabled");
  }
}

/* Print PCI root information */
void DDIOTune::print_dev_info() {

  unsigned int c;
  char namebuf[1024], *name;
  click_chatter("========================\n");
  click_chatter(
      "%04x:%02x:%02x.%d vendor=%04x device=%04x class=%04x irq=%d (pin %d) "
      "base0=%lx \n",
      _dev->domain, _dev->bus, _dev->dev, _dev->func, _dev->vendor_id,
      _dev->device_id, _dev->device_class, _dev->irq, c,
      (long)_dev->base_addr[0]);
  name = pci_lookup_name(pacc, namebuf, sizeof(namebuf), PCI_LOOKUP_DEVICE,
                         _dev->vendor_id, _dev->device_id);
  click_chatter(" (%s)\n", name);
  click_chatter("========================\n");
}

/* Constructor */
DDIOTune::DDIOTune() : _w(-1) {
  eax = 0;
  ebx = 0;
  ecx = 0;
  edx = 0;
  _numa_node = -1;
  _msr_cpu = -1;
  IIO_LLC_WAYS_VALUE_NEW = 0;
  ways = new uint32_t[CACHE_LEVELS];
  partitions = new uint32_t[CACHE_LEVELS];
  linesize = new uint32_t[CACHE_LEVELS];
  sets = new uint32_t[CACHE_LEVELS];
  size = new uint32_t[CACHE_LEVELS];
}

/* Destructor */
DDIOTune::~DDIOTune() {
  delete ways, partitions, linesize, sets, size;

  if (_w) {
    /*Revert IIO_LLC_WAYS value*/
    int ret = wrmsr(IIO_LLC_WAYS_REGISTER, &IIO_LLC_WAYS_VALUE, _msr_cpu);
    if (ret < 0)
      click_chatter("Could not revert IIO_LLC_WAYS value!");
    click_chatter("IIO_LLC_WAYS value changed back to 0x%x from 0x%x",
                  IIO_LLC_WAYS_VALUE, IIO_LLC_WAYS_VALUE_NEW);
  }
  /* Invert DDIO state*/
  if (_ddio_state) {
    ddio_enable();
    click_chatter("DDIO state is reverted back to enabled!");
  } else {
    ddio_disable();
    click_chatter("DDIO state is reverted back to disabled!");
  }
  pci_cleanup(pacc); /* Close everything */
}

/* Configure */
int DDIOTune::configure(Vector<String> &conf, ErrorHandler *errh) {

  String pci_addr;
  struct rte_pci_addr dev_addr;
  if (Args(conf, this, errh)
          .read_p("N_WAYS", _w)    // Number of DDIO Ways
          .read_p("DEV", pci_addr) // PCIe Device, e.g., 0x17 from 0000:17:00.0
          .read("PRINT", _print)   // Print cache info
          .complete() < 0)
    return -1;

  if (rte_pci_addr_parse(pci_addr.c_str(), &dev_addr))
    return errh->error("Invalid PCI address format");
  _pci_dev = dev_addr.bus;
  click_chatter("DDIO device is located on bus 0x%" PRIx8, dev_addr.bus);

  if (!dev_ddiotune[_pci_dev]) {
    dev_ddiotune[_pci_dev] = this;
  } else {
    return errh->error(
        "There can be only one instance of DDIOTune per PCIe bus!");
  }
  return 0;
}

/* Initialize: main function */
int DDIOTune::initialize(ErrorHandler *errh) {

  /* Initialization */
  int pci_access = init_pci_access();
  if (pci_access < 0)
    return errh->error("Could not get PCI data structure!");

  _dev = find_ddio_device(_pci_dev);
  if (!_dev)
    return errh->error("Could not find the proper PCIe root for the device!\n");

  _numa_node = _dev->numa_node;

  uint16_t core_id;
  RTE_LCORE_FOREACH(core_id) {
    int numa_node = rte_lcore_to_socket_id(core_id);
    if (_numa_node == numa_node) {
      _msr_cpu = core_id;
      break;
    }
  }

  /* Get cache information with CPUID instruction */
  for (int i = 0; i < CACHE_LEVELS; i++) {
    /*
     * static __inline int __get_cpuid_count (unsigned int __leaf, unsigned int
     * __subleaf, unsigned int *__eax, unsigned int *__ebx, unsigned int *__ecx,
     * unsigned int *__edx)
     */
    __get_cpuid_count(4, i, &eax, &ebx, &ecx, &edx);

    /* Parse information based on the Intel's reference */
    ways[i] = ((ebx & WAY_MASK) >> WAY_SHIFT) + 1;
    partitions[i] = ((ebx & PARTITION_MASK) >> PARTITION_SHIFT) + 1;
    linesize[i] = ((ebx & LINE_SIZE_MASK) >> LINE_SIZE_SHIFT) + 1;
    sets[i] = ecx + 1;
    size[i] = ways[i] * partitions[i] * linesize[i] * sets[i];
  }

  /* Print cache and PCI root information */
  if (_print) {
    print_cache_info();
    print_dev_info();
  }

  /* Get IIO_LLC_WAYS info */
  max_w = ways[CACHE_LEVELS - 1];
  IIO_LLC_WAYS_VALUE_MIN =
      6 << (max_w - (max_w % 4)); /* The first hex digit is always 6 (i.e.,
                                     Skylake: 0x600 and Haswell: 0x60000). */
  IIO_LLC_WAYS_VALUE_MAX = pow(2, max_w) - 1;

  // click_chatter("Min: 0x%x Max: 0x%x", IIO_LLC_WAYS_VALUE_MIN,
  // IIO_LLC_WAYS_VALUE_MAX);

  /* Read IIO LLC WAYS */
  int ret = rdmsr(IIO_LLC_WAYS_REGISTER, &IIO_LLC_WAYS_VALUE, _msr_cpu);
  if (ret < 0) {
    click_chatter("Could not read IIO_LLC_WAYS!");
    if (getuid()) {
      return errh->error("Need root access! Run again with sudo.");
    } else {
      return errh->error(
          "Make sure that msr module is loaded! Run: sudo modprobe msr");
    }
  }

  if (ddio_status()) {
    click_chatter("DDIO is enabled!");
    _ddio_state = true;
    click_chatter("Current DDIO ways is %d out of %d LLC ways\n",
                  __builtin_popcount(IIO_LLC_WAYS_VALUE), max_w);
  } else {
    _ddio_state = false;
    click_chatter("DDIO is disabled!");
  }

  if (_w == 1)
    return errh->error("N_WAYS cannot be %d. It should be greater than 2 and "
                       "smaller than %d (i.e. number of LLC ways).",
                       _w, max_w);

  if (_w < 0 || _w > max_w)
    return errh->error("N_WAYS cannot be %d. It should be greater than 2 and "
                       "smaller than %d (i.e. number of LLC ways).",
                       _w, max_w);

  if (_w == 0) {
    ddio_disable();
    return 0;
  } else {
    ddio_enable();
    /* Convert _w to the proper hexadecimal value to write */
    IIO_LLC_WAYS_VALUE_NEW = (((uint64_t)(pow(2, _w) - 1)) << (max_w - _w));

    ret = wrmsr(IIO_LLC_WAYS_REGISTER, &IIO_LLC_WAYS_VALUE_NEW, _msr_cpu);
    if (ret < 0)
      return errh->error("Could not write IIO_LLC_WAYS");

    click_chatter("New DDIO ways is %d out of %d LLC ways\n", _w, max_w);
    click_chatter("IIO_LLC_WAYS value changed from 0x%x to 0x%x",
                  IIO_LLC_WAYS_VALUE, IIO_LLC_WAYS_VALUE_NEW);
    return 0;
  }
}

/* Global table of PCIe devices mapped to their DDIOTune objects */
HashTable<uint8_t, DDIOTune *> DDIOTune::dev_ddiotune;

CLICK_ENDDECLS

ELEMENT_REQUIRES(dpdk dpdk18)
ELEMENT_REQUIRES(pci)

EXPORT_ELEMENT(DDIOTune)
