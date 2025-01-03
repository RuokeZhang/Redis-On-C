a command: | nstr | len | str1 | len | str2 | ... | len | strn |
The nstr is the number of strings and the len is the length of the following string. Both are 32-bit integers.

the response: | res | data... |
a 32-bit status code and a response string 

implementation:
BST insertion using pointer

references:
https://github.com/torvalds/linux/blob/master/lib/rbtree.c
https://adtinfo.org/libavl.html/Rebalancing-AVL-Trees.html
