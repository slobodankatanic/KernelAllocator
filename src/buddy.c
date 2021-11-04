#include "buddy.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

struct Block {
	Block* next;
	int block_size[1023];
};

struct Buddy {
	int blocks_num;
	int array_size;
	Block** buddy_array;
	int free_blocks;
};

Buddy* buddy = NULL;
 
void buddy_init(void* start, int blocks_num) {	
	buddy = start;
	buddy->blocks_num = blocks_num - 1;
	//buddy->array_size = log2(blocks_num) + 1;

	//added
	buddy->array_size = (int)log2(buddy->blocks_num) + 1;
	//added

	buddy->buddy_array = (int*)(buddy) + 10;
	for (int i = 0; i < buddy->array_size; i++) {
		buddy->buddy_array[i] = NULL;
	}
	//buddy->buddy_array[buddy->array_size - 1] = (Block*)(start) + 1;
	//buddy->buddy_array[buddy->array_size - 1]->next = NULL;

	//added
	Block* mem = (Block*)(start) + 1;
	for (int i = buddy->array_size - 1; i >= 0; i--) {
		if ((buddy->blocks_num) & (1 << i)) {
			buddy->buddy_array[i] = mem;
			(buddy->buddy_array[i])->next = NULL;
			mem = mem + (int)pow(2, i);
		}
	}
	//added
}

void* buddy_alloc(int n) {
	void* ret = NULL;
	if (buddy->buddy_array[n] != NULL) {
		ret = buddy->buddy_array[n];
		buddy->buddy_array[n] = buddy->buddy_array[n]->next;
		((Block*)ret)->next = NULL;
	}
	else {
		int m = n + 1;
		while (m < buddy->array_size && buddy->buddy_array[m] == NULL) m++;
		if (m < buddy->array_size) {
			ret = buddy->buddy_array[m];
			buddy->buddy_array[m] = buddy->buddy_array[m]->next;
			((Block*)ret)->next = NULL;
			while (m > n) {
				((Block*)ret + (int)pow(2, m - 1))->next = buddy->buddy_array[m - 1];
				buddy->buddy_array[m - 1] = (Block*)ret + (int)pow(2, m - 1);
				m--;
			}
		}
	}
	//printf("ALLOCATED: %d\n", n);
	//buddy_print();
	return ret;
}

int checkBuddies(void* m1, void* m2, int size) {
	int n1 = ((Block*)m1 - (Block*)buddy - 1) / (int)pow(2, size);
	int n2 = ((Block*)m2 - (Block*)buddy - 1) / (int)pow(2, size);
	if (abs(n1 - n2) == 1) {
		if (!(n1 % 2) && m2 == (Block*)m1 + (int)pow(2, size)) {
			return 1;
		}
		else if ((n1 % 2) && m1 == (Block*)m2 + (int)pow(2, size))
			return 2;
	}
	return 0;
}

void delete_node(void* mem, void* prev, int n) {
	if (prev) {
		((Block*)prev)->next = ((Block*)mem)->next;
	}
	else {
		buddy->buddy_array[n] = buddy->buddy_array[n]->next;
	}
	((Block*)mem)->next = NULL;
}

void buddy_dealloc(void* dealloc, int n) {
	Block* mem = buddy->buddy_array[n];
	((Block*)dealloc)->next = NULL;
	int t = 0;
	while (mem) {
		Block* prev = NULL;
		t = checkBuddies(dealloc, mem, n);
		if (t != 0) {
			delete_node(mem, prev, n);
			if (t == 2) {
				dealloc = mem;
			}
			n++;
			mem = buddy->buddy_array[n];
		}
		else {
			prev = mem;
			mem = mem->next;
		}
	}
	((Block*)dealloc)->next = buddy->buddy_array[n];
	buddy->buddy_array[n] = ((Block*)dealloc);
}

void buddy_print() {
	printf("-----------------------------------------\n");
	printf("BROJ BLOKOVA NA RASPOLAGANJU: %d\n", buddy->blocks_num);
	for (int i = 0; i < buddy->array_size; i++) {
		printf("Ulaz%d --> ", i);
		for (Block* block = buddy->buddy_array[i]; block; block = block->next)
			printf("%f --> ", pow(2, i));
		printf("null\n");
	}
	printf("-----------------------------------------\n");
}