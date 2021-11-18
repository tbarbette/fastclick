// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TO_EXTERNAL_PROCESS
#define CLICK_TO_EXTERNAL_PROCESS
#include <click/element.hh>
#include <click/atomic.hh>
#include <click/args.hh>

#include <sys/types.h>
#include <sys/select.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <mutex>

CLICK_DECLS

#define INPUT_INTERFACE 0
#define OUTPUT_INTERFACE  0



class ToExternalProcess : public Element { public:
    ToExternalProcess() CLICK_COLD;

    

    // overriding of the basic methods used by the FastClick infrastructure to catch the object definition
    const char *class_name() const override	{ return "ToExternalProcess"; }
    const char *port_count() const override	{ return "1/1"; }
    const char *processing() const override	{ return PUSH; }

    // method to initialize the object (called by the framework)
    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    
    // destructor, this method is used to free some variable allocated
    ~ToExternalProcess();

    void push(int, Packet *);
    private:
    //identification number of a shared memory allocated by another process,
    //this object not create a shared memory
    int _shared_memory_id; 
    // pointer referencing shared memory
    char* shared_memory;
    // maximum size of raw frame, the default value is equal to Ethernet maximum package size
    int _package_size =1522; 
    // the max number of element who could be putted in the buffer (when it  is full it will be overwrited)
    int _buffer_size; 
    // number of pages the buffer is divided into
    int _number_of_pages;     
    // semaphore identifier, used to signal to the other process that a page is ready to be computed
    int _semaphore_id;
    // this attribute tells if the data have to be sent in full or only the header 
    bool _entire_packet_copy;
    // this method sets the semaphore
    void sendToProcess(const  unsigned char* p_data,unsigned short p_size);
    // header size
    unsigned short _header_size;
    // just a vector of \0 used to fill the memory when the frame is smaller than max package size
    char* empty_data;
    // calculated value, number of row in page
    uint _num_now_in_page=0;
    // mutex used protect the variable _wirite_index increments
    std::mutex _mux;
    // the absolute row number who will be used to save the received frame
    uint _num_actual_row=0;
    //this method sets the semaphore
    bool unlockSemaphor();
};

CLICK_ENDDECLS
#endif
