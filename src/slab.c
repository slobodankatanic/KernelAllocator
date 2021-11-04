#include <math.h>
#include <stdio.h>
#include "slab.h"
#include "buddy.h"
#include <windows.h>

void* all_space = NULL;
int blocks_num = 0;
kmem_cache_t* head_cache = NULL;
kmem_cache_t* tail_cache = NULL;
kmem_cache_t* cache_start = NULL;

HANDLE mutex;

struct FreeSlot {
	FreeSlot* next;
};

struct kmem_cache_t {
	const char* name;
	short is_buffer;
	size_t size;
	Slab* slabs;
	Slab* empty_slabs;
	Slab* full_slabs;
	kmem_cache_t* next;
	Slab* last_slab, *first_slab;
	int alloc_size;
	int total_slots;
	int diff;
	int curr_offset;
	short to_shrink;
	void(*ctor)(void *);
	void(*dtor)(void *);
};

struct Slab {
	Slab* next;
	void* free_slots;
	void* starting_slot;
	void* line_start;
	size_t size;
	int blocks_num;
	int free_slots_num;
	int total_slots;
	kmem_cache_t* cache;
	//char space[4096 * 32];
};

void kmem_init(void *space, int block_num) {
	all_space = space;
	blocks_num = block_num;
	//buddy_init((int*)space + 2048 * (blocks_num - 1025), 1025);
	buddy_init(space, block_num);
	mutex = CreateSemaphore(NULL, 1, 1, NULL);
}

int get_size(size_t size) {
	if (size >= BLOCK_SIZE * 32) return 1;
	else if (size >= BLOCK_SIZE * 16) return 2;
	else if (size >= BLOCK_SIZE * 8) return 4;
	else if (size >= BLOCK_SIZE * 4) return 8;
	else if (size >= BLOCK_SIZE * 2) return 16;
	else if (size >= BLOCK_SIZE / 6) return 32;
	else return BLOCK_SIZE / size;
}

int slab_init(Slab* slab, size_t size, kmem_cache_t* ca) {
	slab->cache = ca;
	slab->next = NULL;
	slab->size = size;

	slab->free_slots = buddy_alloc(slab->cache->alloc_size);
	if (slab->free_slots == NULL) {
		printf("NULETINA1\n");
		return -1;
	}

	slab->starting_slot = slab->free_slots;
	slab->free_slots_num = slab->total_slots = slab->cache->total_slots;
	slab->blocks_num = pow(2, slab->cache->alloc_size);
	slab->line_start = (char*)(slab->free_slots) + slab->cache->curr_offset;
	slab->free_slots = slab->line_start;
	(slab->cache->curr_offset) += CACHE_L1_LINE_SIZE;
	if (slab->cache->curr_offset > slab->cache->diff) slab->cache->curr_offset = 0;
	
	void* curr = slab->free_slots;
	for (int i = 0; i < slab->free_slots_num - 1; i++) {
		((FreeSlot*)curr)->next = (char*)curr + slab->size;
		curr = (char*)curr + slab->size;
	}
	((FreeSlot*)curr)->next = NULL;
	return 1;
}

kmem_cache_t *kmem_cache_create_my(const char *name, size_t size,
	void(*ctor)(void *),
	void(*dtor)(void *)) {
	if (size < 4) size = 8;
	if (head_cache == NULL) {
		tail_cache = head_cache = (kmem_cache_t*)buddy_alloc(0);
		if (tail_cache == NULL) printf("NULETINA2\n");
		cache_start = head_cache;
	}
	else {
		tail_cache->next = tail_cache + 1;
		tail_cache = tail_cache->next;
	}

	tail_cache->name = name;
	tail_cache->next = NULL;
	tail_cache->full_slabs = NULL;
	tail_cache->slabs = NULL;
	tail_cache->last_slab = NULL;
	tail_cache->empty_slabs = NULL;
	tail_cache->first_slab = (Slab*)buddy_alloc(0);
	if (tail_cache->first_slab == NULL) printf("NULETINA3\n");
	//tail_cache->empty_slabs = tail_cache->first_slab;
	//int s = slab_init(tail_cache->empty_slabs, size);
	tail_cache->is_buffer = 0;
	tail_cache->size = size;

	int slots_num = get_size(size);
	int alloc_s = slots_num * size;
	int blocks_num = alloc_s / BLOCK_SIZE + ((alloc_s % BLOCK_SIZE) ? 1 : 0);
	alloc_s = (int)log2(blocks_num);
	alloc_s += (blocks_num == pow(2, alloc_s)) ? 0 : 1;

	tail_cache->alloc_size = alloc_s;
	tail_cache->total_slots = slots_num;
	tail_cache->diff = (pow(2, alloc_s)) * BLOCK_SIZE - slots_num * size;
	tail_cache->curr_offset = 0;
	tail_cache->to_shrink = 1;
	tail_cache->ctor = ctor;
	tail_cache->dtor = dtor;

	return tail_cache;
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size,
								void(*ctor)(void *),
								void(*dtor)(void *)) {
	WaitForSingleObject(mutex, INFINITE);
	kmem_cache_t* ret = kmem_cache_create_my(name, size, ctor, dtor);
	ReleaseSemaphore(mutex, 1, NULL);
	return ret;
}

void *kmem_cache_alloc_my(kmem_cache_t *cache) {
	if (cache == NULL) {
		return NULL;
	}
	void *ret = NULL;
	Slab* s = NULL;
	Slab* head = NULL;

	for (Slab* slab = cache->slabs; slab; slab = slab->next) {
		if (slab->free_slots_num == 0) continue;
		s = slab;
		head = cache->slabs;
		break;
	}
	if (!s) {
		for (Slab* slab = cache->empty_slabs; slab; slab = slab->next) {
			if (slab->free_slots_num == 0) continue;
			s = slab;
			head = cache->empty_slabs;
			break;
		}
	}
	if (!s) {
		//printf("Nema slobodnih slotova\n");
		cache->empty_slabs = (cache->last_slab == NULL) ? (cache->first_slab) : (cache->last_slab + 1);
		int st = slab_init(cache->empty_slabs, cache->size, cache);
		if (st < 0) {
			cache->empty_slabs = NULL;
			return NULL;
		}
		if (cache->last_slab == NULL) cache->last_slab = cache->empty_slabs;
		else cache->last_slab = cache->last_slab + 1;
		s = cache->empty_slabs;
		head = cache->empty_slabs;
		cache->to_shrink = 0;
	}

	ret = s->free_slots;
	s->free_slots = ((FreeSlot*)ret)->next;
	((FreeSlot*)ret)->next = NULL;
	(s->free_slots_num)--;

	if (s->free_slots_num == 0 || s->free_slots_num == s->total_slots - 1) {
		Slab* prev = NULL;
		for (Slab* curr = head; curr != s; curr = curr->next)
			prev = curr;
		if (prev != NULL) {
			prev->next = s->next;
		}
		else {
			if (s == cache->empty_slabs)
				cache->empty_slabs = cache->empty_slabs->next;
			else
				cache->slabs = cache->slabs->next;
		}
		if (s->free_slots_num == 0) {
			s->next = cache->full_slabs;
			cache->full_slabs = s;
		}
		else {
			s->next = cache->slabs;
			cache->slabs = s;
		}
	}

	if (cache->ctor != NULL) cache->ctor(ret);
	return ret;
}

void *kmalloc(size_t size) {
	WaitForSingleObject(mutex, INFINITE); 

	kmem_cache_t* cache = NULL;

	for (kmem_cache_t* curr = head_cache; curr; curr = curr->next) {
		if (curr->is_buffer && curr->size >= size) {
			if (cache == NULL) cache = curr;
			else if (cache->size > curr->size) cache = curr;
		}
	}
	if (!cache) {
		int blks = (int)log2(size);
		blks += (size == pow(2, blks)) ? 0 : 1;
		cache = kmem_cache_create_my("buffer", pow(2, blks), NULL, NULL);
		cache->is_buffer = 1;
	}			
	void* r = kmem_cache_alloc_my(cache);
	ReleaseSemaphore(mutex, 1, NULL);
	return r;
}

void *kmem_cache_alloc(kmem_cache_t *cache) {
	WaitForSingleObject(mutex, INFINITE);
	void* ret = kmem_cache_alloc_my(cache);
	ReleaseSemaphore(mutex, 1, NULL);
	return ret;
}

int is_free(kmem_cache_t *cache, void *objp) {
	for (Slab* s = cache->empty_slabs; s; s = s->next) {
		for (FreeSlot* fs = s->free_slots; fs; fs = fs->next) {
			if (objp == fs) return 1;
		}
	}
	for (Slab* s = cache->slabs; s; s = s->next) {
		for (FreeSlot* fs = s->free_slots; fs; fs = fs->next) {
			if (objp == fs) return 1;
		}
	}
	return 0;
}

int is_part_of_cache(kmem_cache_t* cache, void *objp, Slab** slab, Slab** head) {
	for (Slab* s = cache->slabs; s; s = s->next) {
		for (int i = 0; i < s->total_slots; i++) {
			if (objp == (char*)(s->line_start) + s->size * i) {
				((FreeSlot*)(objp))->next = s->free_slots;
				s->free_slots = objp;
				(s->free_slots_num)++;
				*slab = s;
				*head = cache->slabs;
				return 1;
			}
		}
	}
	for (Slab* s = cache->full_slabs; s; s = s->next) {
		for (int i = 0; i < s->total_slots; i++) {
			if (objp == (char*)(s->line_start) + s->size * i) {
				((FreeSlot*)(objp))->next = s->free_slots;
				s->free_slots = objp;
				(s->free_slots_num)++;
				*slab = s;
				*head = cache->full_slabs;
				return 1;
			}
		}
	}
	return 0;
}

// PAZI KOD BRISANJA DA SE PRAZNI SLABOVI DEALOCIRAJU
void kmem_cache_free(kmem_cache_t *cache, void *objp) {
	 WaitForSingleObject(mutex, INFINITE); 

	if (cache == NULL || objp == NULL || is_free(cache, objp) > 0) {
		ReleaseSemaphore(mutex, 1, NULL); 
		return;
	}

	Slab* slab = NULL;
	Slab* head = NULL;
	int found = 0;

	//izbaci sad ako treba iz liste ovaj slab, slicno kao kraj u preth fji
	if (is_part_of_cache(cache, objp, &slab, &head) == 0) {
		ReleaseSemaphore(mutex, 1, NULL); 
		return;
	}

	if (slab->free_slots_num == slab->total_slots || slab->free_slots_num == 1) {
		Slab* prev = NULL;
		for (Slab* curr = head; curr != slab; curr = curr->next)
			prev = curr;
		if (prev != NULL) {
			prev->next = slab->next;
		}
		else {
			if (slab == cache->full_slabs)
				cache->full_slabs = cache->full_slabs->next;
			else
				cache->slabs = cache->slabs->next;
		}
		if (slab->free_slots_num == 1 && slab->free_slots_num != slab->total_slots) {
			slab->next = cache->slabs;
			cache->slabs = slab;
		}
		else {
			if (slab->free_slots_num == slab->total_slots) {
				slab->next = cache->empty_slabs;
				cache->empty_slabs = slab;
				kmem_cache_shrink_my(cache);
			}
		}
	}

	ReleaseSemaphore(mutex, 1, NULL); 
}

void kfree(const void *objp) {
	WaitForSingleObject(mutex, INFINITE); 
	kmem_cache_t* cache = NULL;
	Slab* slab = NULL, *head = NULL;

	for (kmem_cache_t* c = head_cache; c; c = c->next) {
		if (c->is_buffer && is_free(c, objp) == 0 && is_part_of_cache(c, objp, &slab, &head)) {
			cache = c; break;
		}
	}

	if (slab->free_slots_num == slab->total_slots || slab->free_slots_num == 1) {
		Slab* prev = NULL;
		for (Slab* curr = head; curr != slab; curr = curr->next)
			prev = curr;
		if (prev != NULL) {
			prev->next = slab->next;
		}
		else {
			if (slab == cache->full_slabs)
				cache->full_slabs = cache->full_slabs->next;
			else
				cache->slabs = cache->slabs->next;
		}
		if (slab->free_slots_num == 1 && slab->free_slots_num != slab->total_slots) {
			slab->next = cache->slabs;
			cache->slabs = slab;
		}
		else {
			if (slab->free_slots_num == slab->total_slots) {
				slab->next = cache->empty_slabs;
				cache->empty_slabs = slab;
				kmem_cache_shrink_my(cache);
			}
		}
	}
	ReleaseSemaphore(mutex, 1, NULL); 
}

void kmem_cache_destroy(kmem_cache_t *cachep) {
	WaitForSingleObject(mutex, INFINITE); 

	kmem_cache_t* prev = NULL;
	kmem_cache_t* curr = NULL;

	for (curr = head_cache; curr && curr != cachep; curr = curr->next)
		prev = curr;

	if (!curr) {
		ReleaseSemaphore(mutex, 1, NULL); 
		return;
	}
	
	if (prev) prev->next = curr->next;
	else head_cache = head_cache->next;
	if (curr == tail_cache) tail_cache = prev;
	curr->next = NULL;

	for (Slab* s = curr->empty_slabs; s; s = s->next) 
		buddy_dealloc(s->starting_slot, s->cache->alloc_size);

	for (Slab* s = curr->slabs; s; s = s->next)
		buddy_dealloc(s->starting_slot, s->cache->alloc_size);
	
	for (Slab* s = curr->full_slabs; s; s = s->next)
		buddy_dealloc(s->starting_slot, s->cache->alloc_size);

	buddy_dealloc(curr->first_slab, 0);

	if (!head_cache) buddy_dealloc(cache_start, 0);
	ReleaseSemaphore(mutex, 1, NULL); 
}

int kmem_cache_shrink_my(kmem_cache_t *cachep) {
	if (cachep->to_shrink != 1) {
		cachep->to_shrink = 1;
		return 0;
	}

	int deallocated = 0;
	for (Slab* slab = cachep->empty_slabs; slab; slab = slab->next) {
		buddy_dealloc(slab->starting_slot, slab->cache->alloc_size);
		deallocated += pow(2, slab->cache->alloc_size);
	}

	cachep->empty_slabs = NULL;

	return deallocated;
}

int kmem_cache_shrink(kmem_cache_t *cachep) {
	WaitForSingleObject(mutex, INFINITE); 
	int d = kmem_cache_shrink_my(cachep);
	ReleaseSemaphore(mutex, 1, NULL); 
	return d;
	//moze bolje kao i kod dealociranja keseva
}

void kmem_cache_info(kmem_cache_t *cachep) {
	 WaitForSingleObject(mutex, INFINITE); 

	if (!cachep) {
		ReleaseSemaphore(mutex, 1, NULL); 
		return;
	}

	size_t cache_size = 0;
	int num_of_slabs = 0;
	int total_slots = 0, free_slots = 0;

	for (Slab* s = cachep->full_slabs; s; s = s->next) {
		cache_size += s->blocks_num;
		num_of_slabs++;
		total_slots += s->total_slots;
		free_slots += s->free_slots_num;
	}

	for (Slab* s = cachep->slabs; s; s = s->next) {
		cache_size += s->blocks_num;
		num_of_slabs++;
		total_slots += s->total_slots;
		free_slots += s->free_slots_num;
	}

	for (Slab* s = cachep->empty_slabs; s; s = s->next) {
		cache_size += s->blocks_num;
		num_of_slabs++;
		total_slots += s->total_slots;
		free_slots += s->free_slots_num;
	}

	printf_s("Ime: %s\n", cachep->name);
	printf_s("Velicina podatka : %dB\n", cachep->size);
	printf_s("Velicina kesa : %d\n", cache_size);
	printf_s("Broj ploca : %d\n", num_of_slabs);
	printf_s("Broj objekata u ploci: %d\n", cachep->total_slots);
	printf_s("Popunjenost: %f\n\n", (double)(total_slots - free_slots) / (double)total_slots);

	ReleaseSemaphore(mutex, 1, NULL); 
}

int kmem_cache_error(kmem_cache_t *cachep) {
	 WaitForSingleObject(mutex, INFINITE); 

	printf("Greska\n");

	ReleaseSemaphore(mutex, 1, NULL); 

}