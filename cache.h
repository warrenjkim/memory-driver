#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include <cstdint>

#define L1_CACHE_SETS 16
#define L2_CACHE_SETS 16
#define L2_CACHE_WAYS 8
#define VICTIM_SIZE 4
#define MEM_SIZE 4096
#define BLOCK_SIZE 4

#define HT_L1 1
#define HT_VIC 1
#define HT_L2 8
#define MP 100

typedef enum { L1, VIC, L2 } CacheLevel;

typedef struct CacheBlock {
  int tag = 0;
  int index = 0;
  int lru_position = 0;
  union {
    int data = 0;
    uint8_t byte[4];
  };
  bool valid = false;
} CacheBlock;

typedef struct Stat {
  int l1_miss = 0;
  int l1_hit = 0;

  int l2_miss = 0;
  int l2_hit = 0;

  int vic_miss = 0;
  int vic_hit = 0;
} Stat;

class Cache {
private:
  CacheBlock l1[L1_CACHE_SETS];
  CacheBlock l2[L2_CACHE_SETS][L2_CACHE_WAYS];
  CacheBlock victim[VICTIM_SIZE];

  Stat stat;

public:
  Cache();
  void controller(bool load, bool store, int *data, int addr, int *memory);

  void lw(int addr, int *memory);
  void sw(int *data, int addr, int *memory);

  void evict(const CacheBlock &block, CacheLevel level);
  void update_lru(CacheBlock *arr, CacheLevel level, int position = -1);

  CacheBlock *min_block(CacheLevel level, int index = -1);

  double l1_miss_rate();
  double vic_miss_rate();
  double l2_miss_rate();
};
