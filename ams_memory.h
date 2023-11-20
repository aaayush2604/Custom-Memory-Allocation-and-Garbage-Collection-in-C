#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

volatile sig_atomic_t performGarbageCollection = 0;

int gcIntervalSeconds;

typedef struct __s_block
{
    struct __s_block *next;
    bool isfree;
    size_t size;
    void *memoryAddress;
    bool marked;
} _SBLOCK;

#define BLOCK_SIZE sizeof(_SBLOCK)

static _SBLOCK *sMemBlock = NULL;

#define MAX_ROOT_OBJECTS 10
void *rootObjects[MAX_ROOT_OBJECTS];

void initRootObjects()
{
    for (int i = 0; i < MAX_ROOT_OBJECTS; i++)
    {
        rootObjects[i] = NULL;
    }
}

void addRootObject(void *object)
{
    for (int i = 0; i < MAX_ROOT_OBJECTS; i++)
    {
        if (rootObjects[i] == NULL)
        {
            rootObjects[i] = object;
            break;
        }
    }
}

void removeRootObject(void *object)
{
    for (int i = 0; i < MAX_ROOT_OBJECTS; i++)
    {
        if (rootObjects[i] == object)
        {
            rootObjects[i] = NULL;
        }
    }
}

void initMarkBits(_SBLOCK *head)
{
    _SBLOCK *current = head;
    while (current != NULL)
    {
        current->marked = false;
        current = current->next;
    }
}

void markBlockRecursively(_SBLOCK **head, _SBLOCK *block)
{
    if (block == NULL || block->marked)
    {
        return;
    }
    block->marked = true;
    markBlockRecursively(head, block->next);
}

void markReachableBlocks(_SBLOCK **head)
{
    for (int i = 0; i < MAX_ROOT_OBJECTS; i++)
    {
        if (rootObjects[i] != NULL)
        {
            void *root = rootObjects[i];
            _SBLOCK *current = *head;

            while (current != NULL)
            {
                if (!current->isfree && current->marked)
                {
                    void *block_start = current->memoryAddress;
                    void *block_end = (char *)block_start + current->size;

                    if (block_start <= root && root < block_end)
                    {
                        markBlockRecursively(head, current);
                    }
                }
                current = current->next;
            }
        }
    }
}

void sweepUnmarkedBlocks(_SBLOCK **head)
{
    _SBLOCK *current = *head;
    _SBLOCK *prev = NULL;
    _SBLOCK *next = NULL;

    while (current != NULL)
    {
        if (!current->marked && current->isfree)
        {
            if (prev != NULL)
            {
                prev->next = current->next;
            }
            else
            {
                *head = current->next;
            }

            next = current->next;
        }
        else
        {
            prev = current;
            next = current->next;
        }
        current = next;
    }
}

void garbageCollection()
{
    printf("Initiating Garbage Collection\n");
    initMarkBits(sMemBlock);
    markReachableBlocks(&sMemBlock);
    sweepUnmarkedBlocks(&sMemBlock);
    printf("Garbage Collectio performed\n");
}

void timerHandler(int sig)
{
    if (performGarbageCollection)
    {
        garbageCollection();
    }
}

void startGarbageCollection()
{
    performGarbageCollection = 1;
}

void stopGarbageCollection()
{
    performGarbageCollection = 0;
}

void setupTimer(int gcIntervalSeconds)
{
    struct itimerval timer;
    timer.it_value.tv_sec = gcIntervalSeconds;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = gcIntervalSeconds;
    timer.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1)
    {
        perror("setitimer");
        exit(1);
    }

    signal(SIGALRM, timerHandler);
}

_SBLOCK *allocateMemBlock(size_t size)
{
    _SBLOCK *block = (_SBLOCK *)sbrk(0);
    void *memadr = (void *)sbrk(0);
    void *allocate_mem = (void *)sbrk(BLOCK_SIZE + size);

    if (allocate_mem == (void *)-1)
    {
        return NULL;
    }
    else
    {
        block->next = NULL;
        block->isfree = false;
        block->size = size;
        block->memoryAddress = memadr + BLOCK_SIZE;
        return block;
    }
}

_SBLOCK *allocateNextMemBlock(size_t size, _SBLOCK **head)
{
    _SBLOCK *current = *head;
    void *allocate_mem = NULL;
    void *memadr = (void *)sbrk(0);

    if (current == NULL)
    {
        *head = allocateMemBlock(size);
        return *head;
    }
    else
    {
        while (current->next != NULL)
        {
            current = current->next;
        }
        _SBLOCK *newblock = sbrk(0);
        allocate_mem = (_SBLOCK *)sbrk(BLOCK_SIZE + size);
        if (allocate_mem == (void *)-1)
        {
            return NULL;
        }
        else
        {
            newblock->next = NULL;
            newblock->isfree = false;
            newblock->size = size;
            newblock->memoryAddress = memadr + BLOCK_SIZE;
            current->next = newblock;
            return newblock;
        }
    }
}

_SBLOCK *firstFitAllocate(size_t size, _SBLOCK **head)
{
    _SBLOCK *current = *head;
    _SBLOCK *prev = NULL;

    while (current != NULL)
    {
        if (current->isfree && current->size >= size)
        {
            if (current->size > size + BLOCK_SIZE)
            {
                _SBLOCK *newBlock = (_SBLOCK *)((char *)current + BLOCK_SIZE + size);
                newBlock->next = current->next;
                newBlock->isfree = true;
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->memoryAddress = (void *)((char *)newBlock + BLOCK_SIZE);
                current->next = newBlock;
                current->size = size;
                current->isfree = false;
                return current;
            }
        }

        prev = current;
        current = current->next;
    }

    _SBLOCK *newBlock = allocateNextMemBlock(size, head);
    return newBlock;
}

_SBLOCK *bestFitAllocate(size_t size, _SBLOCK **head)
{
    _SBLOCK *current = *head;
    _SBLOCK *bestFit = NULL;
    _SBLOCK *prev = NULL;

    while (current != NULL)
    {
        if (current->isfree && current->size >= size + BLOCK_SIZE)
        {
            if (bestFit == NULL || current->size < bestFit->size)
            {
                bestFit = current;
            }
        }
        prev = current;
        current = current->next;
    }

    if (bestFit != NULL)
    {
        _SBLOCK *newBlock = (_SBLOCK *)((char *)bestFit + BLOCK_SIZE + size);
        newBlock->next = bestFit->next;
        newBlock->isfree = true;
        newBlock->size = bestFit->size - size - BLOCK_SIZE;
        newBlock->memoryAddress = (void *)((char *)newBlock + BLOCK_SIZE);
        bestFit->next = newBlock;
        bestFit->size = size;
        bestFit->isfree = false;

        return bestFit;
    }

    _SBLOCK *newBlock = allocateNextMemBlock(size, head);
    return newBlock;
}

_SBLOCK *worstFitAllocate(size_t size, _SBLOCK **head)
{
    _SBLOCK *current = *head;
    _SBLOCK *worstFit = NULL;
    _SBLOCK *prev = NULL;

    while (current != NULL)
    {
        if (current->isfree && current->size >= size + BLOCK_SIZE)
        {
            if (worstFit == NULL || current->size > worstFit->size)
            {
                worstFit = current;
            }
        }
        prev = current;
        current = current = current->next;
    }

    if (worstFit != NULL)
    {
        _SBLOCK *newBlock = (_SBLOCK *)((char *)worstFit + BLOCK_SIZE + size);
        newBlock->next = worstFit->next;
        newBlock->isfree = true;
        newBlock->size = worstFit->size - size - BLOCK_SIZE;
        newBlock->memoryAddress = (void *)((char *)newBlock + BLOCK_SIZE);
        worstFit->next = newBlock;
        worstFit->size = size;
        worstFit->isfree = false;

        return worstFit;
    }

    _SBLOCK *newBlock = allocateNextMemBlock(size, head);
    return newBlock;
}

void freeMemBlock(void *block)
{

    _SBLOCK *ogblock = (_SBLOCK *)block;
    if (ogblock == NULL)
    {
    }
    else
    {
        (ogblock)->isfree = true;
    }
}

void printMemBlocks()
{
    _SBLOCK *current = sMemBlock;
    while (current != NULL)
    {
        printf("isfree=%d, size=%d, memoryAddress=%p, current=%p, next-node=%p\n", current->isfree, current->size, current->memoryAddress, current, current->next);
        current = current->next;
    }
    printf("-----<>-----<>-----<>-----\n");
}

void *my_malloc(int allocation_strategy, size_t size)
{
    _SBLOCK *allocatedBlock = NULL;
    switch (allocation_strategy)
    {
    case 1:
        allocatedBlock = firstFitAllocate(size, &sMemBlock);
        break;
    case 2:
        allocatedBlock = bestFitAllocate(size, &sMemBlock);
        break;
    case 3:
        allocatedBlock = worstFitAllocate(size, &sMemBlock);
        break;
    default:
        allocatedBlock = firstFitAllocate(size, &sMemBlock);
        break;
    }

    if (allocatedBlock != NULL)
    {
        return allocatedBlock->memoryAddress;
    }
    else
    {
        allocateNextMemBlock(size, &sMemBlock);
        return my_malloc(allocation_strategy, size);
    }
}

void *my_calloc(int allocation_strategy, size_t num_elements, size_t element_size)
{
    size_t size = num_elements * element_size;
    void *memory = my_malloc(allocation_strategy, size);

    if (memory != NULL)
    {
        memset(memory, 0, size);
    }

    return memory;
}

void my_free(void *ptr)
{
    _SBLOCK *block = sMemBlock;
    _SBLOCK *prev = NULL;

    while (block != NULL)
    {
        if (block->memoryAddress == ptr)
        {
            block->isfree = true;

            if (prev != NULL && prev->isfree)
            {
                prev->size += BLOCK_SIZE + block->size;
                prev->next = block->next;
                block = prev;
            }
            if (block->next != NULL && block->next->isfree)
            {
                block->size += BLOCK_SIZE + block->next->size;
                block->next = block->next->next;
            }
            return;
        }

        prev = block;
        block = block->next;
    }
}