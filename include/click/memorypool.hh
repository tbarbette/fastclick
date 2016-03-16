#ifndef MIDDLEBOX_MEMORYPOOL_HH
#define MIDDLEBOX_MEMORYPOOL_HH

template <typename T>
class MemoryPool
{
public:
    MemoryPool(unsigned int initialSize)
    {
        first = NULL;

        // Perform the first allocation. 'initialSize' memory chunks will be allocated
        allocateMoreMemory(initialSize);
    }

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

    void releaseMemory(T* p)
    {
        ((MemoryPoolNode*)p)->next = first;
        first = (MemoryPoolNode*)p;
    }

private:
    union MemoryPoolNode
    {
        MemoryPoolNode *next;
        T data;
    };

    void allocateMoreMemory(unsigned int size)
    {
        MemoryPoolNode *node = NULL;
        // Creating 'initialSize' elements;
        for(int i = 0; i < size; ++i)
        {
            // Allocate the node and the chunk
            node = (MemoryPoolNode*)malloc(sizeof(MemoryPoolNode));

            node->next = first;
            first = node;
        }
    }

    MemoryPoolNode* first;
};

#endif
