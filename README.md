# cpp-async-tcp

The readme will be done in a few days. As of now this is fully functional, except for one thing:
If a client disconnects from a server (not vice versa), the server doesn't know that the client
has dropped a connection. A way to fix this is to either send a disconnect packet, or to send a
heartbeat packet periodically, as you are able to tell through send() if a client is still connected.
