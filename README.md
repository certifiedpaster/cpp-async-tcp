# cpp-async-tcp

A simple wrapper for sockets. Currently WinSock only, however easily changeable to support Linux.

## Getting started

Clone the repository and include the necessary files in your project. How to set up a server or client
and how to connect is given in the client_/server_main.cpp file.

If you would like to include your own packets, please read packet/packet.h. There, everything is explained
in the form of comments.

## Important notes

When implementing your own packets, remember to use platform independent types so that your client and server
can run on different architectures/OSes.

Additionally, as of now the server has no way of knowing if a client disconnected other than sending a packet
to the client. A workaround for this is for the client to either send a shutdown packet before disconnecting,
or to add a heartbeat packet.
