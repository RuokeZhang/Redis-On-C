#pragma once

#include "avl.h"
#include "hashtable.h"
#include <string>

struct ZSet
{
    AVLNode *tree = NULL;
    HMap hmap;
};

struct ZNode
{
    AVLNode tree; // index by (score, name)
    HNode hmap;   // index by name
    double score = 0;
    size_t len = 0;
    char name[0]; // variable length
};

bool zset_add(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
ZNode *zset_pop(ZSet *zset, const char *name, size_t len);
ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len);
void zset_dispose(ZSet *zset);
ZNode *znode_offset(ZNode *node, int64_t offset);
void znode_del(ZNode *node);