template <typename T, size_t RING_SIZE=256>
class Stack {
protected:
    T ring[RING_SIZE];
    uint32_t head;

public:

    Stack() {
        head = 0;
    }


    /**
     * @pre Stack has >= n elements
     */
    inline T* extract_burst(unsigned n) {
        head -= n;
        T* v = &ring[head];
        return v;
    }

    /**
     * @pre there is enough space for N elements
     */
    inline void insert_burst(T* batch, unsigned n) {
        memcpy(&ring[head], batch, n*sizeof(T));
        head+=n;
    }

    /**
     * @pre there is enough space for the element
     */
    inline void insert(T e) {
        ring[head] = e;
        head+=1;
    }

    inline unsigned int count() {
        return head;
    }

    inline bool has_space() {
        return head < RING_SIZE;
    }

    inline bool is_empty() {
        return head == 0;
    }

};
