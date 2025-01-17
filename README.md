# Redis Database

## global data

  
    static  struct{
	    HMap  db;
		// a map of all client connections, keyed by fd
		std::vector<Conn  *>  fd2conn;
		// timers for idle connections
		DList idle_list;
	} g_data;
g_data is the main database for a Redis process. Once a server starts, it will have a g_data. 
db is an Hmap, contains many HNode. 

## Entry

When the server do some logic to get a resonse(for example, check if a zset exists), it relys on the Entry structure.

    struct  Entry

	{

		struct  HNode  node; //node's hcode is hash(key)

		std::string  key; // name of the zset

		std::string  val;

		uint32_t  type  =  0;

		ZSet  *zset  =  NULL;

	};
To find a zset from the database, we create an Entry for that zset, then pass it's node to find if this node exists in g_data's hmap. 
So we can get a zset in constant time.


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
  
  

# Timeout

Each TCP connection is associated with a timer, which resets to a fixed timeout duration every time there are I/O activities on that connection.

In practice, there will be multiple timers in use. If the `poll()`'s timeout value is set too large, it might still be blocked when a timer should have expired, delaying the handling of the timeout event until another file descriptor (fd) becomes active or the overall timeout occurs. Conversely, setting the timeout value too small can introduce unnecessary overhead because it would cause the loop to run more frequently than necessary.

Therefore, it's ideal to set the `poll()`'s timeout value based on the nearest upcoming timer expiration. This approach minimizes delays in processing timeouts while avoiding excessive polling frequency.

We keep a **linked list** for the timers. Once a timer becomes timeout, we move it to the end of the list. It only takes O(1) time.

## Implementation

When the listening fd accept a new connection, that connection's idle_start will be the time of that moment, and the list node is inserted to the list.

For the connections already in the fd array, when there is an active event, its idle_start will be updated, and its list node will be moved to the head of the list.

There is a function iterating the list, checking if there is timeout to each timer. If timeout, close that connection.

It takes the first timer from the LinkedList, and find its corresponding Connection.

Try this command: `socat tcp:127.0.0.1:3490 -`
And the server outputs:

    ruoke@Ruokes-MacBook-Pro build % ./server  
    server: waiting for connections...
    removing idle connection: 5

# Connection
g_data keeps an array of current connections.
std::vector<Conn  *>  fd2conn;

For a connection, it will have read/write buffer, a file descriptor, state, a list node that represents a timer, and the time when it is established.

Iterate through the fd array, then for fd that has revent set, create a connection for it. Then do connection_io()

A connection is of either STATE_RES or STATE_REQ state.
STATE_RES means there are bytes in the write buffer to be flushed.

**When in STATE_REQ,** the server tries to read data from the buffer.
There could be many requests in the buffer, and the server will handle these requests in a loop. 

 - For a request, the server will parse it, then do some operations to
   the data in the database. The server then fills its response in the
   connection's write buffer, and removes the first request in the read buffer. 
Then change the connection's state to STATE_RES. And the server sends response from the write buffer to the client. After flushing the write buffer, server change state back to STATE_REQ.

Server does this loop over and over again, until there's no enough data in the read buffer. Since it's in STATE_REQ state, the server is still trying to read data from the read buffer, when recv returns 0,  it means the client stop sending message, and server set conn's state to STATE_END, then the conn is killed.
   
## TODO
1. the implementation of hashmap(auto-resizing)
2. string
references:

[Linux implementation of Red Black Tree](https://github.com/torvalds/linux/blob/master/lib/rbtree.c)

https://adtinfo.org/libavl.html/Rebalancing-AVL-Trees.html
