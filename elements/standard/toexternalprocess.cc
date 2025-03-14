#include <click/config.h>
#include <click/error.hh>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <clicknet/ip.h>
#include <clicknet/ether.h>

#include "toexternalprocess.hh"
CLICK_DECLS

union semun
{
    int val;
    struct semid_ds *buf;
    ushort *array;
} semaphor_state;

ToExternalProcess::ToExternalProcess()
{
    empty_data = (char *)malloc(sizeof(char) * _package_size);
    for (int i = 0; i < _package_size; i++)
    {
        empty_data[i] = '\0';
    }
}

int ToExternalProcess::configure(Vector<String> &conf, ErrorHandler *errh)
{
    int buffer_size = 1024 * 1024;
    int number_of_pages = 1024;
    int semaphore_id, shared_memory_id;
    bool enteer_packet_copy = 1;
    uint package_size = 0;

    int output = 0;
    if (Args(conf, this, errh)
            .read_mp("SHARED_MEMORY_ID", shared_memory_id) // read_mp becasue SHARED_MEMOTY_ID is positional mandatory argument (the first one)
            .read_mp("SEMAPHORE_ID", semaphore_id)         // read_mp becasue SEMAPHORE_ID is positional mandatory argument (the second one)
            .read_p("BUFFER_SIZE", buffer_size)
            .read_p("NUMBER_OF_PAGES", number_of_pages)
            .read_p("ENTEER_PACKET", enteer_packet_copy)
            .read_p("PACKAGE_SIZE", package_size)
            .complete() < 0)
    {
        errh->error("Some argument was not provided, has been passed: , SHARED_MEMOTY_ID:'%d', SEMAPHOR_ID:'%d', BUFFER_SIZE:'%d', PAGE_SIZE:'%d', ENTEER_PACKET:'%s'",
                    shared_memory_id, semaphore_id, buffer_size, number_of_pages, enteer_packet_copy);
        return -1;
    }


    if (buffer_size <= 2)
    {
        errh->error("Buffer size must be a value gran the 2, suggested 1024*1024, has benn provided '%d'", buffer_size);
        return -1;
    }
    else if (number_of_pages < 2 || number_of_pages > buffer_size)
    {
        errh->error("The parameter NUMBER_OF_PAGES indicates the pages number , it must be less than the buffer size '%d' and greater than '2'", buffer_size);
        return -1;
    }
    else
    {
        // all parameter is well inizialized
        _shared_memory_id = shared_memory_id;
        _semaphore_id = semaphore_id;
        _buffer_size = buffer_size;
        _number_of_pages = number_of_pages;
        _entire_packet_copy = enteer_packet_copy;

        // if pachage size in sono defined will be used the default value of Ethernet max size (1522)
       if(package_size>0){
            _package_size = package_size;
        }

        _num_now_in_page = _buffer_size / number_of_pages;
        printf("Num Row in page %d\r\n", _num_now_in_page);
    }
    _header_size = (int)sizeof(click_ip) + (int)sizeof(click_ether);

    // Attach shared memory inizialized to the other process on this address space .
    shared_memory = (char *)shmat(shared_memory_id, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        errh->error("Fatal error, shared memopdy hase been not attached. Shared Memory ID:'%d'", _shared_memory_id);
    }


    return 0;
}


// distruttore
ToExternalProcess::~ToExternalProcess()
{
    printf("Free Zero vector\n\n\n");
    free(empty_data);
}
void ToExternalProcess::push(int port, Packet *p)
{
    try
    {
        printf("\033[37m >Inizio lunghezza[%d] \033[37m\r\n", p->length());
        if (_entire_packet_copy)
        {
            sendToProcess(p->data(), p->length());
        }
        else
        {
            sendToProcess(p->data(), _header_size);
        }
        output(OUTPUT_INTERFACE).push(p);
    }
    catch (const std::exception &e)
    {
        printf("Errore nel push\n\n");
    }
}

void ToExternalProcess::sendToProcess(const unsigned char *p_data, unsigned short p_size)
{
    try
    {
        uint l_num_actual_row = 0;
        this->_mux.lock();
        l_num_actual_row = this->_num_actual_row;
        this->_num_actual_row++;

        if (this->_num_actual_row% this->_buffer_size == 0)
        {
            this->_num_actual_row = 0;
            printf("\n\n<jump to a page> \n\n\n");
        }
        
        this->_mux.unlock();

        int spiazzamento = l_num_actual_row * (_package_size + 2);
        char *_start_position = shared_memory + spiazzamento;
        uint16_t package_length = p_size + 2;
        memcpy(_start_position, &package_length, 2);
        memcpy(_start_position + 2, p_data, p_size);
        if (l_num_actual_row % this->_num_now_in_page == 0)
        {
            unlockSemaphor();
        }
    }
    catch (...)
    {
        this->_mux.unlock();
    }
}

bool ToExternalProcess::unlockSemaphor()
{

    union semun arg;
    arg.val = 0;

    if (semctl(_semaphore_id, 0, SETVAL, arg) == -1)
    {
        printf("errore");
    }
    return true;
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ToExternalProcess)
ELEMENT_MT_SAFE(ToExternalProcess)
