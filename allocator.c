#define _DEFAULT_SOURCE
#include <sched.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct header{
    alignas(max_align_t) size_t size;
    bool is_free;
    struct header *next;
} header_t;
static_assert(sizeof(header_t) % alignof(max_align_t) == 0);

static header_t *head = NULL, *tail = NULL;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static header_t *get_free_block(size_t size){
    header_t *curr = head;
    while(curr){
        if(curr -> is_free && curr->size >= size){
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void free(void *block){
    if(!block) return;
    pthread_mutex_lock(&lock);
    header_t *header = (header_t *)block - 1;
    void *programbrk = sbrk(0);
    // Case 1: Block is at the end of the heap -> free the heap and give to OS
    if((char *)block + header -> size == programbrk){
        if(head == tail){
            head = tail = NULL;
        }
        else {
            header_t *tmp = head;
            while (tmp) {
                if (tmp->next == tail) {
                    tmp->next = NULL;
                    tail = tmp;
                    break;
                }
                tmp = tmp->next;
            }
        }
        intptr_t total_size = sizeof(header_t) + header->size;
        sbrk(-total_size);
        pthread_mutex_unlock(&lock);
        return;
    }
    // Case 2: Block is in the middle -> just mark it free for reuse
    header->is_free = true;
    pthread_mutex_unlock(&lock);
}

void *malloc(size_t size){
    size_t total_size;
    header_t *header;
    void *block;
    if(!size){
        return NULL;
    }
    pthread_mutex_lock(&lock);
    header = get_free_block(size);
    if(header){
        header -> is_free = false;
        block = (void *)(header + 1);
        pthread_mutex_unlock(&lock);
        return block;
    }
    total_size = size + sizeof(header_t);
    block = sbrk(total_size);
    if(block == (void *)-1){
        pthread_mutex_unlock(&lock);
        return NULL;
    }
    header = (header_t *)block;
    header -> size = size;
    header -> is_free = false;
    header -> next = NULL;
    if(!head){
        head = header;
    }else{
        tail -> next = header;
    }
    tail = header;
    pthread_mutex_unlock(&lock);
    return (void *)(header + 1);
}

void *calloc(size_t number, size_t size){
    if(!number || !size){
        return NULL;
    }
    size_t total_size = number * size;
    if(size != total_size / number){
        return NULL;
    }
    void *block = malloc(total_size);
    if(!block){
        return NULL;
    }
    memset(block, 0, total_size);
    return block;
}
void *realloc(void *ptr , size_t size){
    if(!ptr){
        return malloc(size);
    }
    if(!size){
        free(ptr);
        return NULL;
    }
    header_t *header;
    void *block;
    header = (header_t *)ptr - 1;
    if(header -> size >= size){
        return ptr;
    }
    block = malloc(size);
    if(!block){
        return NULL;
    }
    memcpy(block, ptr, header -> size);
    free(ptr);
    return block;
}

/* A debug function to print the entire link list */
void print_mem_list()
{
	header_t *curr = head;
	printf("head = %p, tail = %p \n", (void*)head, (void*)tail);
	while(curr) {
		printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
			(void*)curr, curr -> size, curr -> is_free, (void*)curr -> next);
		curr = curr -> next;
	}
}