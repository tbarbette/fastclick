#ifndef CLICK_CTXELEMENT_HH
#define CLICK_CTXELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/router.hh>
#include <click/flowbuffer.hh>
#include <click/flow/common.hh>
#include <click/routervisitor.hh>
#ifdef HAVE_AVX2
#include <immintrin.h>
#endif
#include <algorithm>
#include <click/flow/flowelement.hh>
#include <type_traits>
#include <string>

CLICK_DECLS


struct StackReleaseChain {
    SubFlowRealeaseFnt previous_fnt;
    void* previous_thunk;
};

/*
=c

CTXElement()

=s middlebox

base class for the stack of the middlebox

=d

This element provides a common abstract base for the elements of the stack of the middlebox.
It provides useful methods and the mechanism of function stack. This element is not meant to be used
directly in a click configuration. Instead, use elements that inherit from it.

To use the function stack, simply call one of the methods using this mechanism and the method
will be called automatically on upstream elements until an IPIn element is reached. For instance,
to remove bytes in a packet, elements can simply call removeBytes, giving it the right parameters
and the method will be called on upstream elements that will handle the request and act
consequently.

Elements that inherit from this class can override the processPacket method to define their own
behaviour.

Implementation:

initialize() and solve_initialize() are not accessible, instead you're forced to use the initialization
dependency system. For instance, at the end of your configure() add the following pattern :

    //Set the initialization to run after all stack objects passed
    allStackInitialized.post(new Router::FctFuture([this](ErrorHandler* errh) {
        //Do your initialization here
    }));
    return 0;;

*/

class CTXElement : public VirtualFlowSpaceElement
{
public:
    friend class PathMerger;
    friend class FlowBuffer;
    friend class FlowBufferContentIter;

    /**
     * @brief Construct a CTXElement
     * CTXElement must not be instanciated directly. Consider it as an abstract element.
     */
    CTXElement() CLICK_COLD;

    /**
     * @brief Destruct a CTXElement
     */
    ~CTXElement() CLICK_COLD;

    // Click related methods
    const char *class_name() const        { return "CTXElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return "h/hh"; }
    virtual const size_t flow_data_size() const { return 0; };
    void* cast(const char*) override;

    bool _visited;
    static CounterInitFuture allStackInitialized;
    Router::FctChildFuture _on_fcb_built_future;

    // Custom methods

    /**
     * @brief Method used during the building of the function stack. It sets the element
     * on which we must call the corresponding method to propagate the call in the stack
     * @param element The next element (upstream) in the function stack
     * @param port The input port connected to this element
     */
    virtual void addCTXElementInList(CTXElement* element, int port);

    /**
     * @brief Indicate whether an element is a stack element (which inherits from CTXElement)
     * @param element The element to check
     * @return A boolean indicating whether an element is a stack element
     */
    static bool isCTXElement(Element* element);

    /**
     * @brief Tells the maximum level of modification up to this element.
     * E.g tell if removeBytes and insertBytes are allowed by the current stack
     */
    virtual int maxModificationLevel(Element* stop);


protected:

    /**
     * @brief Set the value of the LAST_USEFUL annotation
     * @param packet The packet
     * @param value The new value for the annotation
     */
    void setAnnotationLastUseful(Packet* packet, bool value) const;

    /**
     * @brief Get the value of the LAST_USEFUL annotation
     * @param packet The packet
     * @return The value of the annotation
     */
    bool getAnnotationLastUseful(Packet* packet) const;

    /**
     * @brief Search a given pattern in the given content. The content does not have
     * to be NULL-terminated.
     * @param content The content in which the pattern will be searched
     * @param pattern The pattern to search
     * @param length The length of the content
     * @return A pointer to the first byte of pattern in the content or NULL if it cannot be found
     */
    inline char* searchInContent(char *content, const StringRef &pattern, uint32_t length);

    /**
     * @brief Set the INITIAL_ACK annotation of the packet. This annotation stores the initial
     * ACK number that the packet had before modification.
     * @param packet The packet
     * @param initialAck The initial ACK number of the packet
     */
    void setInitialAck(Packet *packet, uint32_t initialAck) const;

    /**
     * @brief Get the value of the INITIAL_ACK annotation of the packet.
     * This annotation stores the initial ACK number that the packet had before modification.
     * @param packet The packet
     * @return The initial ACK number of the packet
     */
    uint32_t getInitialAck(Packet *packet) const;

    /**
     * @brief Used to create the function stack. It will run a StackVisitor
     * downstream that will register this element as the next element in the function stack
     * of the next stack element.
     */
    void buildFunctionStack();

    // Methods using the function stack mechanism

    const int MODIFICATION_NONE = 0;
    const int MODIFICATION_WRITABLE = 1; //May write to the packet without resizing
    const int MODIFICATION_STALL = 2;    //May stall packets
    const int MODIFICATION_REPLACE = 4;  //May replace some content
    const int MODIFICATION_RESIZE = 8;   //May resize the packet

    /**
     * @brief Remove bytes in a packet
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     * @param position The position (relative to the current useful content)
     * @param length Number of bytes to remove
     */
    virtual void removeBytes(WritablePacket* packet, uint32_t position,
        uint32_t length);

    /**
     * @brief Insert bytes in a packet. This method creates room for the new bytes and moves
     * the content after the insertion point so that it is after the new bytes.
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     * @param position The position (relative to the current useful content)
     * @param length Number of bytes to insert
     * @return A pointer to the packet with the bytes inserted (can be different from the given
     * pointer)
     */
    virtual WritablePacket* insertBytes(WritablePacket* packet, uint32_t position,
        uint32_t length) CLICK_WARN_UNUSED_RESULT;

    /**
     * @brief Request more packets. Must be used by objects that buffer packets to ensure that
     * they will receive the next packets
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     * @param force A boolean indicating whether the request must be repeated if it as already
     * been done for this packet (default: false)
     */
    virtual void requestMorePackets(Packet *packet, bool force = false);

    /**
     * @brief Close the connection
     * @param fcb A pointer to the FCB of the flow
     * @param packet A packet from the connection, used for initialization
     * @param grafecul A boolean indicating whether the connection must be closed gracefully or not
     */
    virtual void closeConnection(Packet *packet, bool graceful);

    /**
     * @return true if event was handled
     */
    virtual bool registerConnectionClose(StackReleaseChain* fcb_chain, SubFlowRealeaseFnt fnt, void* thunk);

    /**
     * @brief Tells if the current session is established
     * Only applicable to context with connection status
     * @return A boolean indicating whether the connection is established or not
     * of the flow
     */
    virtual bool isEstablished();

    /**
     * @brief Indicate whether a given packet is the last useful one for this side of the flow
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet
     * @return A boolean indicating whether a given packet is the last useful one for this side
     * of the flow
     */
    virtual bool isLastUsefulPacket(Packet *packet);

    /**
     * @brief Determine the flow ID for this path (0 or 1).
     * Each side of a TCP connection has a different flow direction (0 for one of them and 1
     * for the other).
     * This ID is defined in the Click configuration.
     * @return An unsigned int representing the ID (called direction) of the flow in the connection
     */
    virtual unsigned int determineFlowDirection();

private:
    /**
     * @brief Set a bit in the annotation byte used to store booleans. This annotation is used
     * to store up to 8 boolean values
     * @param packet The packet
     * @param bit The offset of the bit (in [0, 7])
     * @param value The new value of the bit
     */
    void setAnnotationBit(Packet* packet, int bit, bool value) const;

    /**
     * @brief Return the value of a bit in the annotation byte used to store booleans. This
     * annotation is used to store up to 8 boolean values
     * @param packet The packet
     * @param bit The offset of the bit (in [0, 7])
     * @return Value of the bit
     */
    bool getAnnotationBit(Packet* packet, int bit) const;

    CTXElement *previousCTXElement; // Previous stack element in the configuration path
                                        // and therefore next element in the function stack.

    // Constants
    // Up to 8 booleans can be stored in the corresponding annotation (see setAnnotationBit)
    const int OFFSET_ANNOTATION_LASTUSEFUL = 0; // Indicates if a packet is the last useful one

};

template<typename T>
class CTXSpaceElement : public CTXElement {
public :
    CTXSpaceElement();

    ~CTXSpaceElement();

    virtual const size_t flow_data_size()  const { return sizeof(T); }

    /**
     * Return the T type for a given FCB
     */
    inline T* fcb_data_for(FlowControlBlock* fcb) {
        T* flowdata = static_cast<T*>((void*)&fcb->data[_flow_data_offset]);
        return flowdata;
    }

    /**
     * Return the T type in the current FCB on the stack
     */
    inline T* fcb_data() {
        return fcb_data_for(fcb_stack);
    }

    void push_batch(int port,PacketBatch* head) final {
        //click_chatter("Pushing packet batch %p with fcb %p in %p{element}",head,fcb_data(),this);
        push_flow(port, fcb_data(), head);
    }

    virtual void push_flow(int port, T* flowdata, PacketBatch* head) = 0;

 };


/**
 * @brief This class defines a RouterVisitor that will be used to build the function stack
 * Each element starts a visitor downsteam so that when the visitor reaches the next CTXElement,
 * the object that started the visitor will be registered as the next element in the function stack
 * of the visited element.
 */
class StackVisitor : public RouterVisitor
{
public:
    /**
     * @brief Construct a StackVisitor
     */
    StackVisitor(CTXElement* startElement)
    {
        this->startElement = startElement;
    }

    /**
     * @brief Destruct a StackVisitor
     */
    ~StackVisitor()
    {

    }

    /**
     * @brief Visit the path of elements until we find a stack element. We will indicate to this
     * element that we are the next element in the function stack so that it will propagate
     * the calls to us. See the Click documentation for the description of the parameters
     */
    bool visit(Element *e, bool, int port, Element*, int, int);

private:
    CTXElement* startElement; // Element that started the visit
};

template<typename T>
CTXSpaceElement<T>::CTXSpaceElement() : CTXElement() {

}

template<typename T>
CTXSpaceElement<T>::~CTXSpaceElement() {

}

template <typename T>
struct BufferData {
    T userdata;
    FlowBuffer flowBuffer;
};



/**
 * CTXStateElement is like CTXSpaceElement but subscribe to the stack for connection open and close events
 *
 * The child must implement :
 * bool new_flow(T*, Packet*);
 * void push_flow(int port, T*, Packet*);
 * void release_flow(T*);
 *
 * This is the equivalent to FlowStateElement but use the last Stack element to
 * manage the state instead of relying on timeout/looking at the packet to learn
 * about a closing state
 */
template<class Derived, typename T> class CTXStateElement : public CTXElement {
    struct AT : public StackReleaseChain {
        T v;
        bool seen;
    };
public :


    CTXStateElement() CLICK_COLD {};
    virtual const size_t flow_data_size()  const { return sizeof(AT); }

    /**
     * CRTP virtual
     */
    inline bool new_flow(T*, Packet*) {
        return true;
    }

    /**
     * Return the T type for a given FCB
     */
    inline T* fcb_data_for(FlowControlBlock* fcb) {
        AT* flowdata = static_cast<AT*>((void*)&fcb->data[_flow_data_offset]);
        return &flowdata->v;
    }

    /**
     * Return the T type in the current FCB on the stack
     */
    inline T* fcb_data() {
        return fcb_data_for(fcb_stack);
    }

    static void release_fnt(FlowControlBlock* fcb, void* thunk ) {
        Derived* derived = static_cast<Derived*>(thunk);
        AT* my_fcb = reinterpret_cast<AT*>(&fcb->data[derived->_flow_data_offset]);
        derived->release_flow(&my_fcb->v);
        if (my_fcb->previous_fnt)
            my_fcb->previous_fnt(fcb, my_fcb->previous_thunk);
    }

    void push_batch(int port,PacketBatch* head) final {
         auto my_fcb = my_fcb_data();
         if (!my_fcb->seen) {
             if (static_cast<Derived*>(this)->new_flow(&my_fcb->v, head->first())) {
                 my_fcb->seen = true;
                 if (!this->registerConnectionClose(my_fcb, &release_fnt, (void*)this)) {
                     click_chatter("ERROR in %p{element}: No element handles the connection",this);
                     abort();
                 }
             } else {
                 head->fast_kill();
             }
         }
         static_cast<Derived*>(this)->push_flow(port, &my_fcb->v, head);
    };


private:
    inline AT* my_fcb_data() {
        return static_cast<AT*>((void*)&fcb_stack->data[_flow_data_offset]);
    }

};

template <class Derived, typename T>
class StackBufferElement : public CTXStateElement<Derived, BufferData<T>>
{
    public:

    const char *processing() const final    { return Element::PUSH; }

    void push_flow(int port, BufferData<T>* fcb_data, PacketBatch* flow)
    {
        auto it = fcb_data->flowBuffer.enqueueAllIter(flow);
        int action = static_cast<Derived*>(this)->process_data(&fcb_data->userdata,it);
        if (action < 0) {
            this->closeConnection((it.current() ? it.current() : flow->first()), true);
            release_flow(fcb_data);
            return;
        } else if (action > 0) {
            this->closeConnection((it.current() ? it.current() : flow->first()), true);
            this->checked_output_push_batch(action,fcb_data->flowBuffer.dequeueAll());
            return;
        }

        PacketBatch* passed = it.flush();
//        click_chatter("Passed %d",passed);
        if (it.current()) {
//            click_chatter("Pending %p",it.current());
            this->requestMorePackets(it.current(), false);
        }
        if (passed)
            this->checked_output_push_batch(action,passed);
    }

    void release_flow(BufferData<T>* fcb) {
        PacketBatch* batch = fcb->flowBuffer.dequeueAll();
        if (batch) {
            batch->fast_kill();
        }
    }
};

template <class Derived, typename T>
class StackChunkBufferElement : public CTXStateElement<Derived, BufferData<T>>
{
    public:

    const char *processing() const final    { return Element::PUSH; }


    /**
     * CRTP virtual
     */
    inline void release_stream(T*) {
        return;
    }


    void push_flow(int port, BufferData<T>* fcb_data, PacketBatch* flow)
    {
        auto it = fcb_data->flowBuffer.enqueueAllChunkIter(flow);
        int action = static_cast<Derived*>(this)->process_data(&fcb_data->userdata,it);
        if (action < 0) {
            this->closeConnection((it.current() ? it.current() : flow->first()), true);
            release_flow(fcb_data);
            return;
        } else if (action > 0) {
            this->closeConnection((it.current() ? it.current() : flow->first()), true);
            this->checked_output_push_batch(action,fcb_data->flowBuffer.dequeueAll());
            return;
        }

        PacketBatch* passed = it.flush();
//        click_chatter("Passed %d",passed);
        if (it.current()) {
//            click_chatter("Pending %p",it.current());
            this->requestMorePackets(it.current(), false);
        }
        if (passed)
            this->checked_output_push_batch(action,passed);
    }

    void release_flow(BufferData<T>* fcb) {
        static_cast<Derived*>(this)->release_stream(&fcb->userdata);
        PacketBatch* batch = fcb->flowBuffer.dequeueAll();
        if (batch) {
            batch->fast_kill();
        }
    }
};

/*
Copyright (c) 2008-2016, Wojciech Muła
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/
namespace bits {

    template <typename T>
    inline T clear_leftmost_set(const T value) {

        assert(value != 0);

        return value & (value - 1);
    }


    template <typename T>
    inline unsigned get_first_bit_set(const T value) {

        assert(value != 0);

        return __builtin_ctz(value);
    }


    template <>
    inline unsigned get_first_bit_set<uint64_t>(const uint64_t value) {

        assert(value != 0);

        return __builtin_ctzl(value);
    }

} // namespace bits

// Code from https://github.com/WojciechMula/sse4-strstr/
#ifdef HAVE_AVX2
inline size_t avx2_strstr_anysize(const char* s, size_t n, const char* needle, size_t k) {

    const __m256i first = _mm256_set1_epi8(needle[0]);
    const __m256i last  = _mm256_set1_epi8(needle[k - 1]);

    for (size_t i = 0; i < n; i += 32) {

        const __m256i block_first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i));
        const __m256i block_last  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + i + k - 1));

        const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
        const __m256i eq_last  = _mm256_cmpeq_epi8(last, block_last);

        uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

        while (mask != 0) {

            const auto bitpos = bits::get_first_bit_set(mask);

            if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                return i + bitpos;
            }

            mask = bits::clear_leftmost_set(mask);
        }
    }

    return std::string::npos;
}
#elif HAVE_SSE42
inline size_t sse42_strstr_anysize(const char* s, size_t n, const char* needle, size_t k) {
    const __m128i N = _mm_loadu_si128((__m128i*)needle);

    for (size_t i = 0; i < n; i += 16) {

        const int mode = _SIDD_UBYTE_OPS
                       | _SIDD_CMP_EQUAL_ORDERED
                       | _SIDD_BIT_MASK;

        const __m128i D   = _mm_loadu_si128((__m128i*)(s + i));
        const __m128i res = _mm_cmpestrm(N, k, D, n - i, mode);
        uint64_t mask = _mm_cvtsi128_si64(res);

        while (mask != 0) {

            const auto bitpos = bits::get_first_bit_set(mask);

            // we know that at least the first character of needle matches
            if (memcmp(s + i + bitpos + 1, needle + 1, k - 1) == 0) {
                return i + bitpos;
            }

            mask = bits::clear_leftmost_set(mask);
        }
    }

    return std::string::npos;
}
#else
inline size_t sse2_strstr_anysize(const char* s, size_t n, const char* needle, size_t k) {

    assert(k > 0);
    assert(n > 0);

    const __m128i first = _mm_set1_epi8(needle[0]);
    const __m128i last  = _mm_set1_epi8(needle[k - 1]);

    for (size_t i = 0; i < n; i += 16) {

        const __m128i block_first = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + i));
        const __m128i block_last  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + i + k - 1));

        const __m128i eq_first = _mm_cmpeq_epi8(first, block_first);
        const __m128i eq_last  = _mm_cmpeq_epi8(last, block_last);

        uint16_t mask = _mm_movemask_epi8(_mm_and_si128(eq_first, eq_last));

        while (mask != 0) {

            const auto bitpos = bits::get_first_bit_set(mask);

            if (memcmp(s + i + bitpos + 1, needle + 1, k - 2) == 0) {
                return i + bitpos;
            }

            mask = bits::clear_leftmost_set(mask);
        }
    }

    return std::string::npos;
}
#endif

//Inline functions
inline char* CTXElement::searchInContent(char *content, const StringRef &pattern, uint32_t length) {
#ifdef HAVE_AVX2
    size_t pos = avx2_strstr_anysize(content, length, pattern.data(), pattern.length());
#elif HAVE_SSE42
    size_t pos = sse42_strstr_anysize(content, length, pattern.data(), pattern.length());
#else
    size_t pos = sse2_strstr_anysize(content, length, pattern.data(), pattern.length());
#endif
    if (pos == std::string::npos)
        return 0;
    else return content + pos;
}

#if HAVE_CTX
extern CounterInitFuture _ctx_builded_init_future;
#endif

CLICK_ENDDECLS

#endif
