# Pluggable randombytes function

[![Travis-CI](https://travis-ci.org/dsprenkels/sss.svg?branch=master)](https://travis-ci.org/dsprenkels/randombytes)
[![Appveyor](https://ci.appveyor.com/api/projects/status/github/dsprenkels/randombytes?branch=master&svg=true)](https://ci.appveyor.com/project/dsprenkels/randombytes)

`randombytes` is a library that exposes a single function for retrieving
_crypto-secure_ random bytes. It is loosely based on [Libsodium's random bytes
API][libsodium_randombytes]. If you can, you should use that one. Otherwise, you
can use this library.

## Usage

`randombytes` takes two arguments:
1. A pointer to the buffer
2. The length of the buffer in bytes

The function will always return an `int` which will be `0` on success. The
caller _must_ check this. If some kind of error occured, `errno` MAY contain a
hint to what the error was, and a subsequent call to the `randombytes` function
MAY succeed. An example of when the function may fail is when `/dev/urandom`
could not be opened, because there were no file descriptors left to use for the
process.

On sensible systems (like the ones with `arc4random`) the latency is very low.
However, this is totally not guaranteed. Do not expect this function to be very
fast. Benchmark for your specific setup, and use a fast CSPRNG if you need.

Example code:

```c
#include "randombytes.h"
#include <inttypes.h>
#include <stdio.h>

int main()
{
    // Generate some random bytes and print them in hex
    int ret;
    uint8_t buf[20];
    size_t i;

    ret = randombytes(&buf[0], sizeof(buf));
    if (ret != 0) {
        printf("Error in `randombytes`");
        return 1;
    }    
    for (i = 0; i < sizeof(buf); ++i) {
        printf("%02hhx", buf[i]);
    }
    printf("\n");
    return 0;
}
```

## How secure is it really?

While building this I keep one rule of thumb which is: **Trust the OS**.
Most OS'es implement a secure random generator, which is seeded by a good
entropy source. We will always use this random source. This essentially means
that the implementation is highly platform-dependent. For example we use
`getrandom` on Linux and `arc4random` on BSD systems.

### What if the OS's random generator is bad?

If you are dealing with an OS that has a compromised random generator you are
out of luck. The reason why you cannot generate high quality random data from
userspace is that userspace is made so that everything is too deterministic.
A secure random generator needs a good source of entropy, such as 2.4 GHz noise
or the user's mouse movements. Collecting these kinds of events only works well
when working on the lowest level.

## Questions

### It does not compile on my platform!

[Please open an issue.](https://github.com/dsprenkels/randombytes/issues/new)
If possible I will try to make a `randombytes` implementation for your platform.

### Do you have bindings for language _x_?

No, your language probably already has a random source. Use that one.

### Other

Feel free to send me an email on my Github associated e-mail address.


[libsodium_randombytes]: https://github.com/jedisct1/libsodium/blob/master/src/libsodium/randombytes/sysrandom/randombytes_sysrandom.c
