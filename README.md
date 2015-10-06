Juggler Proof-of-Work
=======================

**THIS CODE IS EXPERIMENTAL AND SHOULD NOT BE USED FOR ANYTHING.**

Juggler is a proof-of-work puzzle system that requires lots of computing time
*and memory* to solve, but very little memory to verify.

Unfortunately, it *does* require lots of computing time to verify, but it is
a simpler operation, so it is still faster to check than it is to solve. It may
be possible to remove this computational requirement on the verifier by using
fancy crypto voodoo; I don't know yet.

Here are some rough performance characteristics using BLAKE2 reduced to 3 rounds
as the hash function on my AMD FX-8370:

- 0.5GB: 90 seconds proof, 20 seconds verify.
- 1.0GB: 180 seconds proof, 40 seconds verify.

Proof sizes are rather large, at 4152 bytes. But the size is tunable, at the
expense of (I'm guessing) TMTO resistance.

I haven't made any attempt to tune the parameters or even make them consistent
with each other, so the actual performance could be much better or much worse.

If you want to know how it works, look at the code. Sorry I'm tired and would
rather sleep than write up an English explanation right now.

Report bugs/ideas by opening GitHub issues.
