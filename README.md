a command: | nstr | len | str1 | len | str2 | ... | len | strn |
The nstr is the number of strings and the len is the length of the following string. Both are 32-bit integers.

the response: | res | data... |
a 32-bit status code and a response string 

implementation:
BST insertion using pointer

references:
https://github.com/torvalds/linux/blob/master/lib/rbtree.c
https://adtinfo.org/libavl.html/Rebalancing-AVL-Trees.html

Insert a pair: ZADD key score name

Find and remove by name:

ZREM key name
ZSCORE key name
Range query: ZQUERY key score name offset limit

Iterate through the sublist where pair >= (score, name).
Offset the sublist and limit the result size.
Rank query: The offset in the ZQUERY command.

server: g_data里面放zset的名字。zset里面放ZNode（name和score）

test failed:
./client zadd zset 1 n1