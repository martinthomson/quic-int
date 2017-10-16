# QUIC integer encodings

A little test program for benchmarking various forms of integer encoding.

With randomized 62-bit inputs:

```
$ ./bench r 1000
Encoding and decoding 1000 integers over 2000 iterations
--- Type ---     Encode          Decode
memcpy:             1174             762
endian:             6564            4431
highbitbe:         20727           17280
highbitle:         21873           24447
quic:               3597            3567
```

With randomized inputs randomly trimmed to 6-, 14-, and 30-bit values:

```
$ ./bench t 1000
Encoding and decoding 1000 integers over 2000 iterations
--- Type ---     Encode          Decode
memcpy:              774             504
endian:             4404            2428
highbitbe:         15648            9243
highbitle:         13353           12315
quic:               4875            3981
```

With monotonically-increasing inputs (starting at 0):

```
$ ./bench c 1000
Encoding and decoding 1000 integers over 2000 iterations
--- Type ---     Encode          Decode
memcpy:             2052             256
endian:             2808            2108
highbitbe:          8826            4212
highbitle:          4908            5052
quic:               3534            3027
```
