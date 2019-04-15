
# Brainstorming

## Design goals
- Single-threaded
- Little statically linked binary size (<100kb)
- Portable
- Minimalist
- Open a single connection per peer, bidirectional messaging, pipelining, multiplexing

### How do we make it Single-threaded?

We use select(2) because it is the most simple and portable way of doing asynchronous network I/O.

### How do we make it <100kb?

We do not use any libraries and only libc function that can be replaced with a very small wrapper to the kernel system call or a relatively small function.

### How do we have a single connection per peer at a time?

We could implement a multiplexing protocol to allow multiple "sub" connections like Yamux but that's rather more complex solution than below.

We can add a tag number to each command we send, and the response will also include this tag number so we can match it. If we receive a response for a tag number we never sent a message with, we will ignore the response, ruling it out at bogus. The tag number is unique to our peer, if the remote peer sends a request with the same tag number, we just need to reply with that same number in the response. It does not matter if that tag number is the same as the one we sent in a request to that remote peer. We must keep the tag number unique across every requests in a connection we have not received a response for.