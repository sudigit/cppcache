# cppcache

A key-value server in C++

## Run

```sh
make
./build/server                 # terminal 1
./build/client set fruit Apple     # terminal 2
./build/client get fruit
./build/client swap fruit Mango
./build/client get fruit
./build/client del fruit
```

## Done

- TCP server and client
- Length-prefixed request/response protocol
- Non-blocking event loop with `poll()` and pipelining
- Key-value commands: `get`, `set`, `del`, `swap`

## In Progress

- Hashtable
- Data serialization
- AVL tree and sorted set
- Timers and TTL
- Thread pool
