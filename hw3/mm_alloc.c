/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines. Remove this comment and provide
 * a summary of your allocator's design here.
 */

#include "mm_alloc.h"

#include <memory.h>
#include <stdlib.h>
#include <unistd.h>

s_block_ptr list_head = NULL;

/* Split block according to size, b must exist */
void split_block (s_block_ptr b, size_t s)
{
    if (b == NULL || s <= 0) {
        return;
    }

    if(b->size >= s + BLOCK_SIZE) {
        s_block_ptr p = (s_block_ptr) (b->ptr + s);
        p->prev = b;
        if (b->next) {
            (b->next)->prev = p;
        }
        p->next = b->next;
        b->next = p;
        p->size = b->size - s - BLOCK_SIZE;
        p->ptr = b->ptr + s + BLOCK_SIZE;
        b->size = s;
        mm_free(p->ptr);
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
        block = (s_block_ptr)(p - BLOCK_SIZE);
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



void* mm_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    if (list_head == NULL) {
        return extend_heap(NULL, size);
    }

	s_block_ptr prev = NULL;

    for (s_block_ptr head = list_head; head; head = head->next) {
        if (head->is_free == 1 && head->size >= size) {
            head->is_free = 0;
            split_block(head, size);
			return head->ptr;
        }
        prev = head;
    }

    return extend_heap(prev, size);
}


void* mm_realloc(void* ptr, size_t size)
{
    if (size == 0 && ptr == NULL) {
        return NULL;
    } else if (size == 0) {
        mm_free(ptr);
        return NULL;
    } else if (ptr == NULL) {
        return mm_malloc(size);
    }

    s_block_ptr current_block = get_block(ptr);
    if (current_block) {
        void *new_block = mm_malloc(size);
        if (new_block == NULL)
            return NULL;
        size_t s = size;
        if (current_block->size < s) {
            s = current_block ->size;
        }
        memcpy(new_block, ptr, s);
        mm_free(ptr);
        return new_block;
    }
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
