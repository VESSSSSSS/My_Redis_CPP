#include<stddef.h>
#include<stdint.h>

struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0; //哈希码 (利用键通过哈希函数计算而来)
}

struct HTab {
    HNode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};

// 在开始调整大小的时候，先会将h1转移至h2中，且扩充并初始化h1
struct HMap {
    HTab ht1; // 旧表
    HTab ht2; // 新表
    size_t resizing_pos = 0;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_destroy(HMap *hmap);