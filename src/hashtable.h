#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdint.h>

// struct HNode;

struct HNode {
    struct HNode *next;
    uint64_t hcode; // 哈希码 (利用键通过哈希函数计算而来)
};

struct HTab {
    struct HNode **tab;
    size_t mask;
    size_t size;
};

// 在开始调整大小的时候，先会将h1转移至h2中，且扩充并初始化h1
struct HMap {
    struct HTab ht1; // 旧表
    struct HTab ht2; // 新表
    size_t resizing_pos;
};

struct HNode *hm_lookup(struct HMap *hmap, struct HNode *key, bool (*cmp)(struct HNode *, struct HNode *));
void hm_insert(struct HMap *hmap, struct HNode *node);
struct HNode *hm_pop(struct HMap *hmap, struct HNode *key, bool (*cmp)(struct HNode *, struct HNode *));
void hm_destroy(struct HMap *hmap);

#endif

