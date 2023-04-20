/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines. Remove this comment and provide
 * a summary of your allocator's design here.
 */

#include "mm_alloc.h"

#include <stdlib.h>

s_block_ptr list_head = NULL;

/* Split block according to size, b must exist */
void split_block (s_block_ptr b, size_t s) {
    if (b == NULL || s <= 0)
        return;
    if(b->size - s >= BLOCK_SIZE) {
        s_block_ptr new_block = NULL;
        s_block_ptr new_block = ((void *)b + BLOCK_SIZE) + s;
        if (b->next) {
            (b->next)->prev = new_block;
        }
        new_block->prev = b;
        new_block->next = b->next;
        new_block->size = b->size - s - BLOCK_SIZE;
        new_block->ptr = b->ptr + s + BLOCK_SIZE;
        new_block->is_free = 1;

        b->next = new_block;
        b->size = s;

        mm_free(new_block->ptr);
        memset(b->ptr, 0, b->size);
    }
}

/* Try fusing block with neighbors */
s_block_ptr fusion(s_block_ptr b) {
    if (b->next != NULL && b->next->is_free) {
        b->size = b->size + BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) {
            b->next->prev = b;
        }
    }

    if (b->prev != NULL && b->prev->is_free) {
        b->prev->size = b->prev->size + BLOCK_SIZE + b->size;
        b->prev->next = b->next;
        if (b->next != NULL) {
            b->next->prev = b->prev;
        }
        b->prev->is_free = b->is_free;
        b = b->prev;
    }

    return b;
}



/* Get the block from addr */
s_block_ptr get_block (void *p) {
    if (p == NULL) {
        return NULL;
    }

    if (p >= (void *)list_head + BLOCK_SIZE && p <= sbrk(0)) {
        s_block_ptr block = NULL;
        s_block_ptr block = (s_block_ptr)(p - BLOCK_SIZE);
        return block;
    }

    // Return NULL if the pointer is invalid
    return NULL;
}

/* Add a new block at the of heap,
 * return NULL if things go wrong
 */
s_block_ptr extend_heap(s_block_ptr last, size_t s) {
    void *p = sbrk(0);  // Get the current program break
    s_block_ptr new_block;

    if (sbrk(s + BLOCK_SIZE) == (void *)-1) {
        return NULL;  // sbrk failed, return NULL
    }

    new_block = (s_block_ptr)p;
    if (last) {
        last->next = new_block;
    }
    new_block->prev = last;
    new_block->next = NULL;
    new_block->is_free = 0;
    new_block->size = s;
    new_block->ptr = (void *)(new_block) + BLOCK_SIZE;
    memset(new_block->ptr, 0, new_block->size);

    return new_block;
}



void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    if (list_head == NULL) {
        s_block_ptr init_block = extend_heap(NULL, 0);
        if (!init_block) {
            return NULL;
        }
        list_head = init_block;
    }

    s_block_ptr curr = list_head;
    s_block_ptr prev = curr;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            split_block(curr, size);
            curr->is_free = 0;
            return curr->ptr;
        }
        prev = curr;
        curr = curr->next;
    }

    s_block_ptr new_block = extend_heap(prev, size);
    if (!new_block) {
        return NULL;
    }

    return new_block->ptr;
}


void* mm_realloc(void* ptr, size_t size)
{
    return NULL;
}

void mm_free(void* ptr) 
{
    if (ptr == NULL) {
        return;
    }

    s_block_ptr block_to_free = get_block(ptr);
    if (block_to_free == NULL) {
        return;
    }

    block_to_free->is_free = 1;
    fusion(block_to_free);
}
