# Well, hello there!
Welcome to NetBlast. 

# What is it?
Netblast is similar to netperf, in that it is yet another network benchmarking tool. However there are important differences.

In `TCP_STREAM` mode, Netperf opens a single connection from client to server. The client executes a tight loop consisting of a `write()` system call while the server executes a corresponding loop of the `read()` system call.

In `TCP_RR` mode, the client waits for a reply from the server before sending the next message.

So far so good, but many real world applications handle hundreds, if not thousands of connections, and we need to establish benchmarks in `TCP_STREAM` and `TCP_RR` mode. Inevitably, such applications tend to use an `epoll`/`kqueue`/`select` or similar system calls to select "ready" sockets. Netperf neither offers such a `select` mode nor supports multiple connections natively. Thus the need for `NetBlast`

# Comparison between NetBlast, Netperf and Iperf3 
TL;DR
Always prefer Netperf to Iperf3, and use NetBlast if you need to test server performance with large number of connections

Netperf is in general has much more capabilities than Iperf3. In some sense Netperf capabiilities is a superset of Iperf3 capabilities. 

* Netperf offers both `TCP_STREAM` and `TCP_RR` mode whereas Iperf3 offers only `TCP_STREAM` mode.
* Netperf executes a tight `read()` or `write` loop whereas iperf3 executes a `gettimeofday()` and a `select` in addition in the fast path. As a result, for small message sizes, netperf achieves 5 times the throughput of iperf3
* Netperf does not support parallel connections at all, whereas iperf3 does have a `-P` option which is documented as 'parallel connections' however, it is misleading. Suppose you specify `-P 4`. Then iperf3 opens 4 connections, and a single thread and executes a `select()` system call and performs I/O on the ready sockets sequentially. It neither  supports multi-threaded event loops nor supports `epoll` for large number of connections. This is where NetBlast steps in.
* NetBlast supports true multi-threaded event loops and supports `epoll` through the `libev` library
* Netblast provides a `gateway` daemon to orchestrate multiple test cases with varying paramters between a large number of `client` machines and a single `server` machine. (More details below).

# Architecture of NetBlast
TODO

# Building NetBlast
NetBlast has been tested on Debian 8 (squeeze) and mac osx el capitan.

Install the dependencies 
````
sudo apt-get install cmake build-essential libev-dev libxmlrpc-c++8-dev libcurl4-gnutls-dev
````

Build NetBlast
````
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
````

# Running NetBlast
TODO

# Parsing the results
TODO


