#include <bits/pthreadtypes.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct header{
    alignas(max_align_t) size_t size;
    bool is_free;
    struct header *next;
} header_t;

static header_t *head = NULL, *tail = NULL;
pthread_mutex_t lock;

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

int main() {
    
    return 0;
}