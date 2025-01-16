#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
// proj
#include "zset.h"
#include "common.h"
#include "hashtable.h"

// a helper structure for the hashtable lookup
struct HKey
{
    HNode node;
    const char *name = NULL;
    size_t len = 0;
};

static uint32_t min(size_t lhs, size_t rhs)
{
    return lhs < rhs ? lhs : rhs;
}
static bool hcmp(HNode *node, HNode *key)
{
    ZNode *znode = my_container_of(node, ZNode, hmap);
    HKey *hkey = my_container_of(key, HKey, node);
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
    node->hmap.next = NULL;
    node->hmap.hcode = str_hash((uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(node->name, name, len);
    return node;
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
    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    // search ths key in the zset's hmap
    HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? my_container_of(found, ZNode, hmap) : NULL;
}

// compare by the (score, name) tuple
static bool zless(
    AVLNode *lhs, double score, const char *name, size_t len)
{
    assert(lhs != NULL);  // 检查 lhs 是否为 NULL
    assert(name != NULL); // 检查 name 是否为 NULL

    ZNode *zl = my_container_of(lhs, ZNode, tree);
    assert(zl != NULL);       // 确保 my_container_of 返回合法地址
    assert(zl->name != NULL); // 确保 zl->name 不为 NULL

    printf("zless: Comparing score and name\n");
    printf("zless: zl->score = %f, score = %f\n", zl->score, score);

    // 比较分数
    if (zl->score != score)
    {
        return zl->score < score;
    }

    // 比较名字
    size_t cmp_len = min(zl->len, len);
    printf("zless: zl->len = %zu, len = %zu, cmp_len = %zu\n", zl->len, len, cmp_len);

    int rv = memcmp(zl->name, name, cmp_len);
    if (rv != 0)
    {
        return rv < 0;
    }

    // 比较名字长度
    return zl->len < len;
}

static bool zless(AVLNode *lhs, AVLNode *rhs)
{
    assert(lhs != NULL); // 检查 lhs 是否为 NULL
    assert(rhs != NULL); // 检查 rhs 是否为 NULL

    ZNode *zr = my_container_of(rhs, ZNode, tree);
    assert(zr != NULL); // 确保 my_container_of 返回合法地址

    return zless(lhs, zr->score, zr->name, zr->len);
}

static void tree_add(ZSet *zset, ZNode *node)
{
    printf("tree_add: Start\n");

    // 检查 zset 和 node 是否为 NULL
    if (!zset)
    {
        printf("tree_add: zset is NULL\n");
        return;
    }
    if (!node)
    {
        printf("tree_add: node is NULL\n");
        return;
    }

    // 初始化 node->tree
    printf("tree_add: Initializing node tree structure\n");
    node->tree.parent = NULL;
    node->tree.left = NULL;
    node->tree.right = NULL;

    AVLNode *cur = NULL;
    AVLNode **from = &zset->tree; // 指向树的根节点
    printf("tree_add: Starting tree search. Initial zset->tree = %p\n", zset->tree);

    // 遍历树查找插入点
    while (*from)
    {
        cur = *from;
        printf("tree_add: Visiting node %p (left = %p, right = %p, parent = %p)\n",
               cur, cur->left, cur->right, cur->parent);
        if (zless(&node->tree, cur))
        {
            printf("tree_add: Moving left from node %p\n", cur);
            from = &cur->left;
        }
        else
        {
            printf("tree_add: Moving right from node %p\n", cur);
            from = &cur->right;
        }
    }

    // 将新节点插入树中
    printf("tree_add: Found insertion point. Attaching new node at %p\n", *from);
    *from = &node->tree;
    node->tree.parent = cur;

    // 修复 AVL 树的平衡性
    printf("tree_add: Fixing AVL tree balance\n");
    zset->tree = avl_fix(&node->tree);

    printf("tree_add: Finished successfully\n");
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
    printf("zset_add starts, we want to add a new tuple to the zset\n");
    printf("score is %f\n", score);
    // check if ZSet already has this name
    ZNode *node = zset_lookup(zset, name, len);

    // if has, update its score
    if (node)
    {
        printf("zset has this name, we want to update its score\n");
        zset_update(zset, node, score);
        return false;
    }
    else
    { // create a ZNode
        printf("zset doesn't have that name\n");
        ZNode *node = znode_new(name, len, score);
        // add to the hashmap.
        hm_insert(&zset->hmap, &node->hmap);
        printf("successfully insert the znode's hnode to zset's hmap\n");
        // add to the tree
        tree_add(zset, node);
        printf("successfully insert the znode's avlnode to zset's avltree\n");
        return true;
    }
}

// deletion by name
ZNode *zset_pop(ZSet *zset, const char *name, size_t len)
{
    if (!zset->tree)
    {
        printf("zset_pop: zset->tree is NULL\n");
        return NULL;
    }
    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    // search the key in the zset's hmap, try to delete it
    HNode *found = hm_pop(&zset->hmap, &key.node, &hcmp);
    if (!found)
    {
        return NULL;
    }
    // if zset has that tuple, remove it from the tree as well
    // get the ZNode from the HNode
    ZNode *node = my_container_of(found, ZNode, hmap);
    // get the tree
    zset->tree = avl_del(&node->tree);
    return node;
}

// find the (score, name) tuple that is greater or equal to the argument.
ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len)
{
    AVLNode *found = NULL;
    for (AVLNode *cur = zset->tree; cur;)
    {
        if (zless(cur, score, name, len))
        {
            cur = cur->right;
        }
        else
        {
            found = cur;
            cur = cur->left;
        }
    }
    return found ? my_container_of(found, ZNode, tree) : NULL;
}

// offset into the succeeding or preceding node.
ZNode *znode_offset(ZNode *node, int64_t offset)
{
    AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
    return tnode ? my_container_of(tnode, ZNode, tree) : NULL;
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
    znode_del(my_container_of(node, ZNode, tree));
}

// destroy the zset
void zset_dispose(ZSet *zset)
{
    tree_dispose(zset->tree);
    hm_destroy(&zset->hmap);
}