#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

/*
using intrusive data structure
*/

// a hashtable node
struct HNode
{
    HNode *next = NULL;
    uint64_t hcode = 0;
} struct HTab
{
    HNode **tab = NULL;
    size_t mask = 0; // 2^n-1
    size_t size = 0;
}

const size_t k_resizing_work = 128;

static void
h_init(Htab *htab, size_t n)
{
    assert(n > 0 && (n & (n - 1)) == 0); // Check if n is a power of 2
    htab->tab = (HNode **)calloc(sizeof(HNode *), n);
    htab->mask = n - 1;
    htab->size = 0;
}

// hashtable insertion
static void h_insert(Htab *htab, HNode *node)
{
    // get the node's position(hash index)
    size_t pos = node->hcode & htab->mask;
    // next can be NULL or another node with the same hash index
    HNode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

// return a pointer to *Hnode. Hnode has the key we want.
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode *, HNode *))
{
    // if the current table is empty
    if (!htab->tab)
    {
        return NULL;
    }
    // get the key's position
    size_t pos = key->hcode & htab->mask;
    //
    HNode **from = &htab->tab[pos];
    HNode *cur = *from;
    while (cur != NULL)
    {
        if (cur->hcode == key->hcode && eq(cur, key))
        {
            return from;
        }
        from = &cur->next;
        cur = *from;
    }
    return NULL;
}

// remove the first node from the chain. from is the address of tab[pos] for some pos
static HNode *h_detach(HTab *htab, HNode **from)
{
    HNode *node = *from;
    *from = node->next;
    htab->size--;
    return node;
}

struct HMap
{
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
}

const size_t k_max_load_factor = 8;

void hm_insert(HMap *hamp, HNode *node)
{
    if (!hmap->ht1.tab)
    {
        h_init(&hmao->ht1, 4);
    }
    h_insert(&hmap->ht1, node);
    // check the load factor
    if (!hmap->ht2.tab)
    {
        size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
        if (load_factor > k_max_load_factor)
        {
            hm_start_resizing(hmap);
        }
    }
    // move keys to the newer table
    hm_help_resizing(hmap);
}

static void hm_start_resizing(HMap *hmap)
{
    assert(hmap->ht2.tab == NULL);
    // create a bigger hashtable, and swap them
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}

// it’s triggered from both lookups and updates
static void hm_help_resizing(HMap *hmap)
{
    size_t nwork = 0;
    while (nwork < k_resizing_work && hmap->ht2.size > 0)
    {
        // find nodes in ht2, then move them to ht1
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from)
        {
            hmap->resizing_pos++;
            continue;
        }
        // insert that node into ht1, remove the first node in that position in ht2
        HNode *toInserted = h_detach(&hmap->ht2, from);
        h_insert(&hmap->ht1, toInsterted);
        nwork++;
    }
    if (hmap->ht2.size == 0 && hmap->ht2.tab)
    {
        // done
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

// search for the key in both hashtables.
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
    hm_help_resizing(hmap);
    HNode **node = h_lookup(&hmap->ht1, key, eq);
    if (!node)
    {
        node = h_lookup(&hmap->ht2, key, eq);
    }
    return node ? *node : NULL;
}

// pop a node from the hashmap, and then return it
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
    hm_help_resizing(hmap);
    // search for that key in ht1
    HNode **node = h_lookup(&hmap->ht1, key, eq);
    if (node)
    {
        return h_detach(&hmap->ht1, node);
    }
    // search for that key in ht1
    HNode **node = h_lookup(&hmap->ht2, key, eq);
    if (node)
    {
        return h_detach(&hmap->ht2, node);
    }
    return NULL;
}

size_t hm_size(HMap *hmap)
{
    return hmap->ht1.size + hmap->ht2.size;
}

void hm_destroy(HMap *hmap)
{
    free(hmap->ht1.tab);
    free(hmap->ht2.tab);
    *hmap = HMap{};
}