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
  * fast memory allocation
  */
template <typename T>
class MemoryPool
{
public:
    /** @brief Construct a MemoryPool
     * @param initialSize The initial number of elements in the pool
     */
    MemoryPool(unsigned int initialSize)
    {
        first = NULL;

        // Perform the first allocation. 'initialSize' memory chunks will be
        // allocated
        allocateMoreMemory(initialSize);
    }

    /** @brief Destruct the memory pool
     */
    ~MemoryPool()
    {
        MemoryPoolNode *node = first;
        MemoryPoolNode *toDelete = NULL;

        // Search the node in the full list
        while(node != NULL)
        {
            toDelete = node;
            node = node->next;

            // Free the node
            free(toDelete);
        }
    }

    /** @brief Obtain memory from the memory pool
     * @return A pointer to an object of the type T
     */
    T* getMemory()
    {
        MemoryPoolNode *node = first;

        if(first == NULL)
        {
            allocateMoreMemory(1);
            node = first;
        }

        first = node->next;

        return (T*)node;
    }

    /** @brief Release memory and put it back to the pool
     * @param p Pointer the the object of type T
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
    };

    /** @brief Allocate more memory for the pool
     * @param size Number of elements to allocate
     */
    void allocateMoreMemory(unsigned int size)
    {
        MemoryPoolNode *node = NULL;
        // Creating 'initialSize' elements;
        for(int i = 0; i < size; ++i)
        {
            // Allocate the node
            node = (MemoryPoolNode*)malloc(sizeof(MemoryPoolNode));

            node->next = first;
            first = node;
        }
    }

    MemoryPoolNode* first; /** Pointer to the first element in the pool.
                               NULL if the pool is empty */
};

#endif
