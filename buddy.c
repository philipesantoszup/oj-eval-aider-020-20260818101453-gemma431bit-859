#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buddy.h"

#define PAGE_SIZE 4096
#define MAX_RANK 16
#define MAX_PAGES 131072

typedef struct block {
    struct block *next;
} block_t;

static void *base_addr = NULL;
static int total_pages = 0;
static block_t *free_lists[MAX_RANK + 1];
static int page_rank[MAX_PAGES];
static char page_allocated[MAX_PAGES];
static char is_start_of_alloc[MAX_PAGES];

static void add_to_free_list(void *p, int rank) {
    block_t *b = (block_t *)p;
    b->next = free_lists[rank];
    free_lists[rank] = b;
}

static void remove_from_free_list(void *p, int rank) {
    block_t **curr = &free_lists[rank];
    while (*curr) {
        if (*curr == (block_t *)p) {
            *curr = (*curr)->next;
            return;
        }
        curr = &((*curr)->next);
    }
}

static void *pop_from_free_list(int rank) {
    if (!free_lists[rank]) return NULL;
    block_t *b = free_lists[rank];
    free_lists[rank] = b->next;
    return (void *)b;
}

static int is_buddy_free(void *p, int rank) {
    block_t *curr = free_lists[rank];
    while (curr) {
        if (curr == (block_t *)p) return 1;
        curr = curr->next;
    }
    return 0;
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }
    memset(page_rank, 0, sizeof(page_rank));
    memset(page_allocated, 0, sizeof(page_allocated));
    memset(is_start_of_alloc, 0, sizeof(is_start_of_alloc));

    int offset = 0;
    while (offset < pgcount) {
        int r = MAX_RANK;
        while (r >= 1) {
            int size = 1 << (r - 1);
            if (size <= (pgcount - offset) && (offset % size == 0)) {
                break;
            }
            r--;
        }
        void *block_p = (char *)p + (size_t)offset * PAGE_SIZE;
        add_to_free_list(block_p, r);
        for (int j = offset; j < offset + (1 << (r - 1)); j++) {
            page_rank[j] = r;
            page_allocated[j] = 0;
        }
        offset += (1 << (r - 1));
    }
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    for (int r = rank; r <= MAX_RANK; r++) {
        void *block = pop_from_free_list(r);
        if (block) {
            int curr_r = r;
            while (curr_r > rank) {
                curr_r--;
                void *buddy = (char *)block + (1 << (curr_r - 1)) * PAGE_SIZE;
                add_to_free_list(buddy, curr_r);
                
                int buddy_idx = ((char *)buddy - (char *)base_addr) / PAGE_SIZE;
                for (int j = buddy_idx; j < buddy_idx + (1 << (curr_r - 1)); j++) {
                    page_rank[j] = curr_r;
                    page_allocated[j] = 0;
                }
                int block_idx = ((char *)block - (char *)base_addr) / PAGE_SIZE;
                for (int j = block_idx; j < block_idx + (1 << (curr_r - 1)); j++) {
                    page_rank[j] = curr_r;
                    page_allocated[j] = 0;
                }
            }
            
            int block_idx = ((char *)block - (char *)base_addr) / PAGE_SIZE;
            for (int j = block_idx; j < block_idx + (1 << (rank - 1)); j++) {
                page_rank[j] = rank;
                page_allocated[j] = 1;
            }
            is_start_of_alloc[block_idx] = 1;
            return block;
        }
    }
    return ERR_PTR(-ENOSPC);
}

int return_pages(void *p) {
    if (!base_addr || (char *)p < (char *)base_addr || (char *)p >= (char *)base_addr + (size_t)total_pages * PAGE_SIZE) {
        return -EINVAL;
    }
    int idx = ((char *)p - (char *)base_addr) / PAGE_SIZE;
    if (!is_start_of_alloc[idx]) {
        return -EINVAL;
    }

    int rank = page_rank[idx];
    is_start_of_alloc[idx] = 0;
    for (int j = idx; j < idx + (1 << (rank - 1)); j++) {
        page_allocated[j] = 0;
    }

    void *curr_p = p;
    int curr_rank = rank;

    while (curr_rank < MAX_RANK) {
        size_t offset_pages = ((char *)curr_p - (char *)base_addr) / PAGE_SIZE;
        size_t buddy_pages = offset_pages ^ (1 << (curr_rank - 1));
        
        if (buddy_pages >= (size_t)total_pages) break;

        void *buddy_p = (char *)base_addr + buddy_pages * PAGE_SIZE;
        
        if (is_buddy_free(buddy_p, curr_rank)) {
            remove_from_free_list(buddy_p, curr_rank);
            if (buddy_pages < offset_pages) {
                curr_p = buddy_p;
            }
            curr_rank++;
            int merged_idx = ((char *)curr_p - (char *)base_addr) / PAGE_SIZE;
            for (int j = merged_idx; j < merged_idx + (1 << (curr_rank - 1)); j++) {
                page_rank[j] = curr_rank;
                page_allocated[j] = 0;
            }
        } else {
            break;
        }
    }

    add_to_free_list(curr_p, curr_rank);
    return OK;
}

int query_ranks(void *p) {
    if (!base_addr || (char *)p < (char *)base_addr || (char *)p >= (char *)base_addr + (size_t)total_pages * PAGE_SIZE) {
        return -EINVAL;
    }
    int idx = ((char *)p - (char *)base_addr) / PAGE_SIZE;
    return page_rank[idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    int count = 0;
    block_t *curr = free_lists[rank];
    while (curr) {
        count++;
        curr = curr->next;
    }
    return count;
}
