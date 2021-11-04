#ifndef _BUDDY_ALLOCATOR_
#define _BUDDY_ALLOCATOR_

typedef struct Block Block;
typedef struct Buddy Buddy;

void buddy_init(void* start, int n);
void* buddy_alloc(int n);
void buddy_dealloc(void*, int n);
void buddy_print();

#endif 
