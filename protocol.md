# Protocol

Draft for the first version of the **unilink** protocol.

## Header

Always sent before any command.

| Flags  | Tag     | Type    | Version | Size    |
| :----: | :-----: | :-----: | :-----: | :-----: |
| 8 bits | 32 bits | 16 bits | 16 bits | 32 bits |

### Flags

| Bit offset | Meaning if activated | Meaning if deactivated |
| :--------: | :------------------: | :--------------------: |
| 0          | Command is a request | Command is a response  |
| 1 to 7     | Reserved             | Reserved               |

### Tag

Unsigned integer in network byte order.

When a peer A sends a command to a peer B, peer A sets the tag to an arbitrary value, when peer B sends a response to that request to peer A, it sets the tag to the same value, peer A will not send a request to peer B for which it has not received a response for from peer B with the same tag.

All requests sent that have not received a response must have a unique tag value.

This behavior allows to pipeline multiple requests in the same TCP connection and receive responses for these requests in any order.

### Type

Unsigned integer in network byte order, it describes the type of the command.

| Value      | Command type |
| :--------: | :----------: |
| 0          | Ping         |
| 1          | Announce     |
| 2 to 65535 | Reserved     |

### Version

Unsigned integer in network byte order, it describes the version of the command to allow for backwards compatibility.

### Size

Unsigned integer in network byte order, it describes the amount of octets following the header that are included in the command.

## Commands

### Ping

Command most commonly used to test a peer for protocol conformance and availability.

When a peer receives a request of type ping, it must send a response back with identical header but the flags value which must have the bit at offset 0 deactivated to indicate a response, and *size* identical octets following it or none if size is zero.

### Announce

_Address block_

| Family | Size    | Address data      |
| :----: | :-----: | :---------------: |
| 4 bits | 12 bits | 8 bits x **Size** |

--

| Role   | Address block count | Address blocks                            | Public key type | Public key size | Public key                   | Signature size | Signature                   | Master signature type | Master signature size | Master signature                   |
| :----: | :-----------------: | :---------------------------------------: | :-------------: | :-------------: | :--------------------------: | :------------: | :-------------------------: | :-------------------: | :-------------------: | :--------------------------------: |
| 8 bits | 8 bits              | _Address block_ x **Address block count** | 4 bits          | 12 bits         | 8 bits x **Public key size** | 16 bits        | 8 bits x **Signature size** | 8 bits                | 16 bits               | 8 bits x **Master signature size** |