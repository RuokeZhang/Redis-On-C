#include <assert.h>
#include <string.h>
#include <stdlib.h>
// proj
#include "zset.h"
#include "common.h"

struct ZSet
{
    HMap hmap;
    AVLNode *tree = NULL;
} struct ZNode
{
    AVLNode tree; // index by (score, name)
    HNode hmap;   // index by name
    double score = 0;
    size_t len = 0;
    char name[0];
} struct HKey
{
    HNode node;
    const char *name = NULL;
    size_t len = 0;
};

static bool hcmp(HNode *node, HNode *key)
{
    ZNode *znode = container_of(node, ZNode, hmap);
    HKey *hkey = container_of(key, HKey, node);
    if (znode->len != hkey->len)
    {
        return false;
    }
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}

static ZNode *
znode_new(const char *name, size_t len, double score)
{
    ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
    avl_init(&node->tree);
    node->hmap = NULL;
    node->score = score;
    node->len = len;
    memcpy(name->node, name, len);
}
// lookup by name.
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len)
{
    // if zset has no tree at all, return NULL
    if (!zset->tree)
    {
        return NULL;
    }
    // it's a local variable, so there's no need to allocate space for it
    Hkey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    // search ths key in the zset's hmap
    HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}

// compare by the (score, name) tuple
static bool zless(
    AVLNode *lhs, double score, const char *name, size_t len)
{
    ZNode *zl = container_of(lhs, ZNode, tree);
    if (zl->score != score)
    {
        return zl->score < score;
    }
    int rv = memcmp(zl->name, name, min(zl->len, len));
    if (rv != 0)
    {
        return rv < 0;
    }
    return zl->len < len;
}

static uint32_t min(size_t lhs, size_t rhs)
{
    return lhs < rhs ? lhs : rhs;
}

static bool zless(AVLNode *lhs, AVLNode *rhs)
{
    ZNode *zr = container_of(rhs, ZNode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

static void tree_add(ZSet *zset, ZNode *node)
{
    AVLNode *cur = NULL;          // current node
    AVLNode **from = &zset->tree; // the incoming pointer to the next node
    while (*from)
    { // tree search
        cur = *from;
        from = zless(&node->tree, cur) ? &cur->left : &cur->right;
    }
    *from = &node->tree; // attach the new node
    node->tree.parent = cur;
    zset->tree = avl_fix(&node->tree);
}

// update the score of an existing node (AVL tree reinsertion)
static void zset_update(ZSet *zset, ZNode *node, double score)
{
    if (node->score == score)
    {
        return;
    }
    zset->tree = avl_del(&node->tree);
    node->score = score;
    avl_init(&node->tree);
    tree_add(zset, node);
}

// add a new (score, name) tuple, or update the score of the existing tuple
bool zset_add(ZSet *zset, const char *name, size_t len, double score)
{
    // check if ZSet already has this name
    ZNode *node = zset_lookup(&zset, &name, len);
    // if has, update its score
    if (node)
    {
        zset_update(&zset, &node, score);
    }
    else
    { // create a ZNode
        ZNode *node = znode_new(&name, len, score);
        // add to the hashmap.
        hm_insert(&zset->hmap, node->hmap);
        // add to the tree
        tree_add(&zset, &node);
    }
}

// deletion by name
ZNode *zset_pop(ZSet *zset, const char *name, size_t len)
{
    if (!zset->tree)
    {
        return NULL;
    }
    Hkey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    // search ths key in the zset's hmap, try to delete it
    HNode *found = hm_pop(&zset->hmap, &key.node, &hcmp);
    if (!found)
    {
        return NULL;
    }
    // if zset has that tuple, remove it from the tree as well
    // get the ZNode from the HNode
    ZNode *node = container_of(found, ZNode, hmap);
    // get the tree
    zset->tree = avl_del(node->tree);
}

// find the (score, name) tuple that is greater or equal to the argument.
ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len)
{
    AVLNode *found = NULL;
    for (AVLNode *cur = zset->tree; cur;)
    {
        if (zless(cur, score, &name, len))
        {
            cur = cur->right;
        }
        else
        {
            found = cur;
            cur = cur->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : NULL;
}

// offset into the succeeding or preceding node.
ZNode *znode_offset(ZNode *node, int64_t offset)
{
    AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
    return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

void znode_del(ZNode *node)
{
    free(node);
}

static void tree_dispose(AVLNode *node)
{
    if (!node)
    {
        return;
    }
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(container_of(node, ZNode, tree));
}

// destroy the zset
void zset_dispose(ZSet *zset)
{
    tree_dispose(zset->tree);
    hm_destroy(&zset->hmap);
}