# ZSet(Sorted Set)

A ZSet structure is implemented using a AVLTree and a Hashmap.

    struct  ZNode{

	AVLNode  tree; // index by (score, name)

	HNode  hmap; // index by name

	double  score  =  0;

	size_t  len  =  0;

	char  name[0]; // variable length};

## Sorted Set Operations

1. Insert a pair: `ZADD key score name`, return 1 if added successfully.
	

       ./client ZADD student 20 age 
       (int)1
	  It takes log time, as inserting a node into an AVL tree takes log time.

2. Find a value by name: `ZSCORE key name`
	

        ./client zscore student age
         (dbl) 20
	It takes constant time. We create a HNode that has the hcode=hash(name), when we search it in ZSet's hmap, and we get an HNode. Then use container_of to get its ZNode, and output its value.

	   

   
3. Remove a pair by name:   `ZREM key name`. Returns 1 if removed successfully.
	

        ./client zrem student age   
	    (int)1
	It takes log time since removing a node from AVL tree takes log time.

4. Range query: `ZQUERY key score name offset limit`
  

        ./client zquery student 18 anna 0 3
	    (arr) len=4
	    (str) tom
	    (dbl) 20.2
	    (str) ben
	    (dbl) 22.2
	    (arr) end

	The core implementation of this operation is to find the node that of the specified offset. It's realized by AVL tree traversal(each node has its rank), so it takes log time.
  
  

## TODO
1. the implementation of hashmap(auto-resizing)
references:

[Linux implementation of Red Black Tree](https://github.com/torvalds/linux/blob/master/lib/rbtree.c)

https://adtinfo.org/libavl.html/Rebalancing-AVL-Trees.html
