#include "allocator.h"
#include <string.h>
#include <stdbool.h>

static void *base;
static size_t used;

typedef struct metadata {
  size_t sz;
  int used;
  int prev_used;
  struct metadata* next;
  struct metadata* prev;
  struct metadata* prev_physical;
} metadata;

static metadata* last_block;

static metadata* small_free_list;
static metadata* medium_free_list;
static metadata* large_free_list;

static int getFreeListIndex(size_t size) {
  if (size <= 64) {
    return 0; 
  } else if (size <= 1024) {
    return 1; 
  } else {
    return 2; 
  }
}

static metadata** getFreeList(int index) {
  if (index == 0) return &small_free_list;
  else if (index == 1) return &medium_free_list;
  else return &large_free_list;
}

void addNode(metadata* mt) {
  if (!mt) return;
  mt->used = 0;
  metadata* next_block = (metadata*)((void*)mt + mt->sz);
  if ((void*)next_block < (void*)(base + used)) {
    next_block->prev_used = 0;
    next_block->prev_physical = mt;
  }
  int list_index = getFreeListIndex(mt->sz);
  metadata** free_list_ptr = getFreeList(list_index);
  mt->next = *free_list_ptr;
  mt->prev = NULL;
  
  if (*free_list_ptr) {
    (*free_list_ptr)->prev = mt;
  }
  *free_list_ptr = mt;
}

void removeNode(metadata* mt) {
  if (!mt) return;
  int list_index = getFreeListIndex(mt->sz);
  metadata** free_list_ptr = getFreeList(list_index);
  if (*free_list_ptr == mt) {
      *free_list_ptr = mt->next;
  }
  if (mt->prev) {
      mt->prev->next = mt->next;
  }
  if (mt->next) {
      mt->next->prev = mt->prev;
  }
  mt->next = NULL;
  mt->prev = NULL;
  mt->used = 1;
}


void allocator_init(void *newbase) {
  base = newbase;
  used = 0;
  small_free_list = NULL;
  medium_free_list = NULL;
  large_free_list = NULL;
  last_block = NULL;
}

void allocator_reset() {
  used = 0;
  small_free_list = NULL;
  medium_free_list = NULL;
  large_free_list = NULL;
  last_block = NULL;
}

void* mymalloc(size_t size) {
  size_t block_size = sizeof(metadata) + size;
  if (block_size % 8 != 0) {
    block_size += 8 - (block_size % 8);
  }
  
  if (block_size < (sizeof(metadata) + 8)) {
    block_size = (sizeof(metadata) + 8);
  }
  
  metadata* block = NULL;
  int index_t = getFreeListIndex(size);
  for (int i = 0; i < 3; i++) {
    index_t = index_t%3;
    metadata* current;
    if (index_t == 0) current = small_free_list;
    else if (index_t == 1) current = medium_free_list;
    else current = large_free_list;
    
    while (current) {
      if (current->sz >= block_size) {
        block = current;
        removeNode(block);
        break;
      }
      current = current->next;
    }
    if (block) break;
    index_t++;
  }
  if (block) {
    size_t original_size = block->sz;
    if (original_size >= block_size + (sizeof(metadata) + 8)) {
      metadata* free_block = (metadata*)((void*)block + block_size);
      free_block->sz = original_size - block_size;
      free_block->used = 0;
      free_block->prev_used = 1;
      free_block->prev_physical = block;
      free_block->next = NULL;
      free_block->prev = NULL;
      block->sz = block_size;
      metadata* next_block = (metadata*)((void*)free_block + free_block->sz);
      if ((void*)next_block < (void*)((void*)base + used)) {
        next_block->prev_physical = free_block;
      }
      addNode(free_block);
    }
    block->used = 1;
    metadata* next_physical = (metadata*)((void*)block + block->sz);
    if ((void*)next_physical < (void*)(base + used)) {
      next_physical->prev_used = 1;
    }
  return ((void*)block + sizeof(metadata));
  }
  block = (metadata*)((void*)base + used);
  block->sz = block_size;
  block->used = 1;
  block->next = NULL;
  block->prev = NULL;
  if (last_block) {
    block->prev_physical = last_block;
    block->prev_used = last_block->used;
  } else {
    block->prev_physical = NULL;
    block->prev_used = 0;
  }
  last_block = block;
  used += block_size;
  return ((void*)block + sizeof(metadata));
}

void myfree(void* ptr) {
  if (!ptr) return;
  metadata* block = (metadata*)(ptr - sizeof(metadata));
  if ((void*)block < base || (void*)block >= (void*)(base + used)) {
    return;
  }

  block->used = 0;
  if ((void*)((void*)block + block->sz) == (void*)(base + used)) {
    metadata* prev_block = block->prev_physical;
    if (prev_block && !prev_block->used) {
      removeNode(prev_block);
      prev_block->sz += block->sz;
      last_block = prev_block;
      used -= block->sz;
      return;
    }
    used -= block->sz;
    if (block->prev_physical) {
      last_block = block->prev_physical;
    } else {
      last_block = NULL;
    }
    return; 
  }
  metadata* prev_block = block->prev_physical;
  metadata* next_block = (metadata*)((void*)block + block->sz);
  bool next_is_free = false;
  if ((void*)next_block < (void*)(base + used) && !next_block->used) {
    next_is_free = true;
  }
  bool prev_is_free = false;
  if (prev_block && !prev_block->used) {
    prev_is_free = true;
  }
  if (prev_is_free && next_is_free) {
    removeNode(prev_block);
    removeNode(next_block);
    prev_block->sz += block->sz + next_block->sz;
    metadata* nnb = (metadata*)((void*)next_block + next_block->sz);
    if ((void*)nnb < (void*)(base + used)) {
      nnb->prev_physical = prev_block;
      nnb->prev_used = 0;
    }
    addNode(prev_block);
    return;
  } 
  else if (prev_is_free) {
    removeNode(prev_block);
    prev_block->sz += block->sz;
    if ((void*)next_block < (void*)(base + used)) {
      next_block->prev_physical = prev_block;
      next_block->prev_used = 0;
    }
    addNode(prev_block);
    return;
  } 
  else if (next_is_free) {
    removeNode(next_block);
    
    block->sz += next_block->sz;
    
    metadata* next_next_block = (metadata*)((void*)next_block + next_block->sz);
    if ((void*)next_next_block < (void*)(base + used)) {
      next_next_block->prev_physical = block;
      next_next_block->prev_used = 0;
    }
    addNode(block);
    return;
  } 
  else {
    if ((void*)next_block < (void*)(base + used)) {
      next_block->prev_used = 0;
    }
    addNode(block);
  }
}


void* myrealloc(void* ptr, size_t size) {
  if (!ptr) return mymalloc(size);
  if (size == 0) {
    myfree(ptr);
    return NULL;
  }
  metadata* block = (metadata*)((void*)ptr - sizeof(metadata));
  if ((void*)block < base || (void*)block >= (void*)(base + used)) {
    return NULL;
  }
  if (!block->used) {
    return NULL;
  }
  size_t block_size = sizeof(metadata) + size;
  if (block_size % 8 != 0) {
    block_size += 8 - (block_size % 8);
  }
  
  if (block_size < (sizeof(metadata) + 8)) {
    block_size = (sizeof(metadata) + 8);
  }
  if (((void*)block + block->sz) == (base + used)) {
    size_t old_size = block->sz;
    block->sz = block_size;
    used += (block_size - old_size);
    return ptr;
  }
  if (block_size < block->sz) {
    size_t original_size = block->sz;
//shrink mechanism
    if (original_size >= block_size + (sizeof(metadata) + 8)) {
      metadata* free_block = (metadata*)((void*)block + block_size);
      free_block->sz = original_size - block_size;
      free_block->used = 0;
      free_block->prev_used = 1;
      free_block->prev_physical = block;
      free_block->next = NULL;
      free_block->prev = NULL;
      block->sz = block_size;
      
      metadata* next_block = (metadata*)((void*)free_block + free_block->sz);
      if ((void*)next_block < (void*)(base + used)) {
        next_block->prev_physical = free_block;
      }
      addNode(free_block);
    }
  return ptr;
  }
  if (block_size == block->sz) {
    return ptr;
  }
  
  metadata* next_block = (metadata*)((void*)block + block->sz);
  if ((void*)next_block < (void*)(base + used) && !next_block->used && block->sz + next_block->sz >= block_size) {
    removeNode(next_block);
    // if the next one is not in use, we might've just use their memory
    size_t combined_size = block->sz + next_block->sz;
    size_t original_size = combined_size;
    
    if (original_size >= block_size + (sizeof(metadata) + 8)) {
      metadata* free_block = (metadata*)((void*)block + block_size);
      
      free_block->sz = original_size - block_size;
      free_block->used = 0;
      free_block->prev_used = 1;
      free_block->prev_physical = block;
      free_block->next = NULL;
      free_block->prev = NULL;
      metadata* next_next_block = (metadata*)((void*)free_block + free_block->sz);
      if ((void*)next_next_block < (void*)(base + used)) {
        next_next_block->prev_physical = free_block;
      }
      
      block->sz = block_size;
      addNode(free_block);
    } else {
      block->sz = combined_size;
      metadata* next_next_block = (metadata*)((void*)next_block + next_block->sz);
      if ((void*)next_next_block < (void*)(base + used)) {
        next_next_block->prev_physical = block;
      }
    }
    return ptr;
  }
  void* new_ptr = mymalloc(size);
  if (!new_ptr) return NULL;
  size_t copy_size = block->sz - sizeof(metadata);
  if (copy_size > size) copy_size = size;
  memcpy(new_ptr, ptr, copy_size);
  myfree(ptr);
  return new_ptr;
}
