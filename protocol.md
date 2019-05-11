# Protocol

## Header

The header is sent before every request or response

| flags  |   tag   |  type   | version |  size   |
|--------|---------|---------|---------|---------|
| 8 bits | 32 bits | 16 bits | 16 bits | 32 bits |

flags bits:
- 0
If this bit's value is 1 then the command is a request, otherwise it's a response.
- 1
Unused
- 2
Unused
- 3
Unused
- 4
Unused
- 5
Unused
- 6
Unused
- 7
Unused

## Ping


