#ifndef MIDDLEBOX_MEMORYPOOL_HH
#define MIDDLEBOX_MEMORYPOOL_HH

/*
 * memorypool.hh - Template for a memory pool
 *
 * Romain Gaillard.
 */

 /** @class MemoryPool
  * @brief Template for memory pools
  *
  * This template is used to create memory pools that can be used to achieve
  * fast memory allocation. It is not multi-thread safe, but can be duplicated
  * per-thread iif objects are always released on the same core they
  * were allocated.
  */
template <typename T>
class MemoryPool
{
    int const DEFAULT_ALLOC = 1024;
public:
    /** @brief Construct an empty MemoryPool
     */
    MemoryPool()
    {
        first = NULL;
    }

    /** @brief Construct a MemoryPool
     * @param initialSize The initial number of elements in the pool
     */
    MemoryPool(unsigned int initialSize)
    {
        first = NULL;

        // Perform the first allocation. 'initialSize' memory chunks will be
        // allocated
        initialize(initialSize);
    }

    /** @brief Destruct the memory pool
     */
    ~MemoryPool()
    {
        MemoryPoolNode *node = first;
        MemoryPoolNode *toDelete = NULL;

        // Free each node of the list
        while(node != NULL)
        {
            toDelete = node;
            node = node->next;

            // Free the node
            delete toDelete;
        }
    }

    /** @brief Initialize the MemoryPool
     * @param initialSize The initial number of elements in the pool
     */
    void initialize(unsigned int initialSize)
    {
        if(first != NULL)
            return;

        // Perform the first allocation. 'initialSize' memory chunks will be
        // allocated
        allocateMoreMemory(initialSize);
    }

    /** @brief Obtain memory from the memory pool
     * @return A pointer to an object of type T
     */
    T* getMemory()
    {
        MemoryPoolNode *node = first;

        if(first == NULL)
        {
            allocateMoreMemory(DEFAULT_ALLOC);
            node = first;
        }

        first = node->next;

        return (T*)node;
    }

    /** @brief Release memory and put it back in the pool
     * @param p Pointer to the object of type T
     */
    void releaseMemory(T* p)
    {
        ((MemoryPoolNode*)p)->next = first;
        first = (MemoryPoolNode*)p;
    }

private:
    // Element of the pool
    union MemoryPoolNode
    {
        MemoryPoolNode *next; /** Pointer to the next element */
        T data; /** Object of type T stored in the pool */

        // Constructors and destructors required since T can be an object with a constructor
        // that will be deleted from the union (C++11)
        MemoryPoolNode()
        {
            next = NULL;
        }

        ~MemoryPoolNode()
        {

        }
    };

    /** @brief Allocate more memory for the pool
     * @param size Number of elements to allocate
     */
    void allocateMoreMemory(unsigned int size)
    {
        MemoryPoolNode *node = NULL;
        // Creating 'initialSize' elements;
        for(unsigned i = 0; i < size; ++i)
        {
            // Allocate the node
            node = new MemoryPoolNode;

            node->next = first;
            first = node;
        }
    }

    MemoryPoolNode* first; /** Pointer to the first element in the pool.
                               NULL if the pool is empty */
};

#endif
