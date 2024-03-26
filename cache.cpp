#include "cache.h"

Cache::Cache() {
  for (int i = 0; i < L1_CACHE_SETS; i++)
    l1[i].valid = false;

  for (int i = 0; i < L2_CACHE_SETS; i++)
    for (int j = 0; j < L2_CACHE_WAYS; j++)
      l2[i][j].valid = false;
}

void Cache::controller(bool load, bool store, int *data, int addr,
                       int *memory) {
  if (load)
    lw(addr, memory);

  if (store)
    sw(data, addr, memory);
}

void Cache::lw(int addr, int *memory) {
  bool l1_hit = false;
  bool vic_hit = false;
  bool l2_hit = false;

  size_t block_offset = 0x3 & addr;
  int index = (addr >> 2) & 0xF;
  int tag = addr >> 6;

  // try l1
  if (this->l1[index].index == index && this->l1[index].tag == tag &&
      this->l1[index].valid)
    l1_hit = true;

  // try victim
  if (!l1_hit) {
    tag = addr >> 2;
    for (auto &entry : this->victim) {
      if (entry.tag == tag && entry.valid) {
        // block to (potentially) evict in l1
        CacheBlock block = *min_block(L1, index);

        // evict block in l1 and set correct tag
        this->l1[index] = entry;
        this->l1[index].tag = addr >> 6;

        // insert evicted block -> victim ->l2 if valid
        if (block.valid)
          evict(block, VIC);

        // evict entry from cache level
        entry.valid = false;
        entry.lru_position = 0;

        vic_hit = true;
        break;
      }
    }
  }

  // try l2
  if (!l1_hit && !vic_hit) {
    tag = addr >> 6;
    for (auto &entry : this->l2[index]) {
      if (entry.index == index && entry.tag == tag && entry.valid) {
        // block to (potentially) evict in l1
        CacheBlock block = *min_block(L1, index);

        // evict block in l1 and set correct tag
        this->l1[index] = entry;
        this->l1[index].tag = tag;

        // insert evicted block -> victim -> l2 if valid
        if (block.valid)
          evict(block, VIC);

        // evict entry from cache level
        entry.valid = false;

        l2_hit = true;
        break;
      }
    }
  }

  // go to memory
  if (!l1_hit && !vic_hit && !l2_hit) {
    // block to (potentially) evict in l1
    CacheBlock block = *min_block(L1, index);

    // fetch the block data
    for (size_t i = 0; i < BLOCK_SIZE; i++)
      this->l1[index].byte[i] = memory[addr - block_offset + i];

    this->l1[index].tag = tag;
    this->l1[index].valid = true;
    this->l1[index].index = index;

    // insert evicted block -> victim -> l2 if valid
    if (block.valid)
      evict(block, VIC);
  }

  stat.l1_miss += !l1_hit;
  stat.l1_hit += l1_hit;

  stat.vic_miss += !l1_hit && !vic_hit;
  stat.vic_hit += vic_hit;

  stat.l2_miss += !l1_hit && !vic_hit && !l2_hit;
  stat.l2_hit += l2_hit;
}

void Cache::sw(int *data, int addr, int *memory) {
  bool l1_hit = false;
  bool vic_hit = false;

  size_t block_offset = 0x3 & addr;
  int index = (addr >> 2) & 0xF;
  int tag = addr >> 6;

  // search l1
  if (this->l1[index].tag == tag && this->l1[index].valid) {
    // write-through
    this->l1[index].byte[block_offset] = *data;
    this->l1[index].valid = true;

    l1_hit = true;
  }

  // search victim
  if (!l1_hit) {
    tag = addr >> 2;
    for (auto &entry : this->victim) {
      if (entry.tag == tag && entry.valid) {
        // write-through
        entry.byte[block_offset] = *data;
        entry.valid = true;

        // update LRU
        size_t position = entry.lru_position;
        entry.lru_position = VICTIM_SIZE;
        update_lru(this->victim, VIC, position);

        vic_hit = true;
        break;
      }
    }
  }

  // check l2
  if (!l1_hit && !vic_hit) {
    tag = addr >> 6;
    for (auto &entry : this->l2[index]) {
      if (entry.tag == tag && entry.valid) {
        // write-through
        entry.byte[block_offset] = *data;
        entry.valid = true;

        // update LRU
        size_t position = entry.lru_position;
        entry.lru_position = VICTIM_SIZE;
        update_lru(this->l2[index], L2, position);
        break;
      }
    }
  }

  // write-no-allocate
  memory[addr] = *data;
}

void Cache::update_lru(CacheBlock *arr, CacheLevel level, int position) {
  size_t size = 0;

  // size of array is determined by the cache level
  // l1 has no LRU since it's direct mapped
  if (level == VIC) {
    size = VICTIM_SIZE;
  } else if (level == L2)
    size = L2_CACHE_WAYS;

  // decrement all valid entries
  for (size_t i = 0; i < size; i++) {
    if (arr[i].valid) {
      // default is to not decrement if lru_position == 0
      // this check is here for sw (updating something in the middle)
      if (arr[i].lru_position < position)
        continue;

      // evict element with lru position of 0
      if (arr[i].lru_position == 0)
        arr[i].valid = false;

      arr[i].lru_position--;
    }
  }
}

void Cache::evict(const CacheBlock &block, CacheLevel level) {
  // base case
  if (level == L2) {
    // get the block to evict
    CacheBlock *min = min_block(L2, block.index);
    *min = block;
    // update LRU
    min->lru_position = L2_CACHE_WAYS;
    update_lru(this->l2[block.index], L2);

    // set the correct tag for the l2 cache level
    //
    // the block will always come from the victim cache so we can safely assume
    // that the tag will be 30 bits, so >> 4 will yield the correct tag
    min->tag = block.tag >> 4;

    return;
  }

  // recursive case
  if (level == VIC) {
    // get the block to evict
    CacheBlock *min = min_block(VIC);
    CacheBlock to_evict = *min;

    // update victim cache with l1 block
    *min = block;

    // update LRU
    min->lru_position = VICTIM_SIZE;
    update_lru(this->victim, VIC);

    // set the correct tag for the victim cache level
    //
    // the block will always come from the l1 cache so we can safely assume that
    // the tag and index will be 26 and 4 bits respectively, so tag << 4 | index
    // will yield the correct tag
    min->tag = (block.tag << 4) | block.index;

    // insert evicted block -> l2 if valid
    if (to_evict.valid)
      evict(to_evict, L2);
  }
}

CacheBlock *Cache::min_block(CacheLevel level, int index) {
  CacheBlock *min;

  // l1 is direct mapped, so return the block at index
  //
  // victim is fully-associative, so return the LRU entry or the next empty
  // entry, prioritizing empty entries
  //
  // l2 is set-associative, so return the LRU entry or the next empty entry in
  // the block at index, prioritizing empty entries
  switch (level) {
  case L1:
    return &this->l1[index];

  case VIC:
    min = this->victim;

    for (auto &entry : this->victim)
      if (!entry.valid)
        return &entry;

    for (auto &entry : this->victim)
      if (entry.lru_position < min->lru_position && entry.valid)
        min = &entry;

    return min;

  case L2:
    min = this->l2[index];

    for (auto &entry : this->l2[index])
      if (!entry.valid)
        return &entry;

    for (auto &entry : this->l2[index])
      if (entry.lru_position < min->lru_position && entry.valid)
        min = &entry;

    return min;
  }
}

double Cache::l1_miss_rate() {
  if (stat.l1_miss == 0 && stat.l1_hit == 0)
    return (double)(0);
  return (double)(stat.l1_miss) / (stat.l1_miss + stat.l1_hit);
}

double Cache::vic_miss_rate() {
  if (stat.vic_miss == 0 && stat.vic_hit == 0)
    return (double)(0);
  return (double)(stat.vic_miss) / (stat.vic_miss + stat.vic_hit);
}

double Cache::l2_miss_rate() {
  if (stat.l2_miss == 0 && stat.l2_hit == 0)
    return (double)(0);

  return (double)(stat.l2_miss) / (stat.l2_miss + stat.l2_hit);
}
