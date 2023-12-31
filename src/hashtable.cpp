#include<assert.h>
#include<stdlib.h>
#include "hashtable.h"

static void h_init(HTab *htab , size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab = (HNode **)calloc(n , sizeof(HNode *));
    htab->mask = n - 1; // 利用哈希码，然后通过对应的掩码，从而找到相应的哈希桶索引
    htab->size = 0;
}

// 插入
static void h_insert(HTab *htab , HNode *node) {
    size_t pos = htab->mask & node->hcode;
    HNode *next = htab->tab[pos]->next;
    htab->tab[pos]->next = node;
    node->next = next;
    htab->size++;
}

// 查找 (指定了哈希表进行查找) 找到的是包含了其地址的父节点
static HNode **h_lookup(HTab *htab , HNode *key , bool(*cmp)(HNode * , HNode*)) {
    if(!htab->tab) {
        return NULL;
    }

    size_t pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[pos];
    while(*from) {
        if(cmp(*from , key)) {
            return from;
        }
        from = &(*from)->next;
    }
    return NULL;
}

// 删除一个节点，从一个桶索引所包含的链表中
static HNode *h_detach(HTab *htab, HNode **from) {
    HNode *node = *from;
    *from = (*from)->next;
    htab->size--;
    return node;
}

const size_t k_resizing_work = 128;

static void hm_help_resizing(HMap *hmap) {
    if (hmap->ht2.tab == NULL) {
        return ;
    }

    size_t nwork = 0;
    while(nwork < k_resizing_work && hmap->ht2.size > 0) {
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from) {
            hmap->resizing_pos++;
            continue;
        }

        h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
        nwork++;
    }

    if((hmap->ht2).size == 0) {
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

static void hm_start_resizing(HMap *hmap) {
    assert(hmap->ht2.tab == NULL);

    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}

HNode *hm_lookup(HMap *hmap , HNode*key , bool (*cmp)(HNode * , HNode *)) {
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if(!from) {
        from = h_lookup(&hmap->ht2, key, cmp);
    }

    return from ? *from : NULL;
}

// 此处定义的负载因子 = 已占用节点数 / 哈希桶的数量
const size_t k_max_load_factor = 8; 

void hm_insert(HMap *hmap , HNode *node) {
    if(!hmap->ht1.tab) {
        h_init(&hmap->ht1 , 4);
    }
    h_insert(&(hmap->ht1), node);

    if(!hmap->ht2.tab) {
        // 检查是否需要调整大小
        size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
        if(load_factor >= k_max_load_factor) {
            hm_start_resizing(hmap);
        }
    }
    hm_help_resizing(hmap);
}

HNode* hm_pop(HMap *hmap , HNode *key , bool (*cmp)(HNode *, HNode *)) {
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if(from) {
        return h_detach(&hmap->ht1, from);
    }

    from = h_lookup(&hmap->ht2, key, cmp);
    if(from) {
        return h_detach(&hmap->ht2, from);
    }
    return NULL;
}

void hm_destroy(HMap *hmap) {
    assert(hmap->ht1.size + hmap->ht2.size == 0);
    free(&hmap->ht1);
    free(&hmap->ht2);
    *hmap = HMap{};
}