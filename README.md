# CS144 — A TCP/IP Stack in Modern C++ (minnow)

> A working TCP/IP protocol stack built from the ground up — reliable byte stream,
> stream reassembler, standards-compliant TCP receiver and sender, an IP-over-Ethernet
> network interface with ARP, and a longest-prefix-match IP router — an independent,
> from-skeleton implementation of **Stanford CS144 — Introduction to Computer Networking**,
> part of a [csdiy.wiki](https://csdiy.wiki/) full-catalog build.

![status](https://img.shields.io/badge/status-complete-brightgreen)
![language](https://img.shields.io/badge/C%2B%2B20-informational)
![license](https://img.shields.io/badge/license-MIT-blue)

## Overview

Stanford CS144 has students build a real TCP/IP stack in modern C++ (the 2023+
"minnow" codebase), one layer at a time, and verifies each layer against the same
test suite the course staff use. This repository implements every checkpoint from
the empty skeleton: an in-memory reliable byte stream, a reassembler that stitches
out-of-order/overlapping substrings back into order, a TCP receiver and sender that
interoperate with real TCP implementations, a network interface that carries IP over
Ethernet using ARP, and an IP router that forwards datagrams by longest-prefix match.

The result is standards-compliant: the sender/receiver pair correctly handle SYN/FIN,
sequence-number wrapping, flow-control windows, zero-window probing, and retransmission
with exponential backoff.

## Results (measured on WSL2 Ubuntu 24.04, gcc 13.3.0, x86-64)

Each test is run **twice** by the course's own harness — once compiled with
Address/UB sanitizers, once optimized — plus speed benchmarks.

```
ctest --test-dir build -E 't_webget'
=> 100% tests passed, 0 tests failed out of 38
```

| Checkpoint | What it does | Result (measured, ctest) |
|---|---|---|
| 0 — Byte stream + webget | In-memory reliable byte stream; HTTP GET over TCP | byte_stream **9/9**; webget verified vs. example.com |
| 1 — Reassembler | Reorder/coalesce overlapping substrings into the stream | **17/17** |
| 2 — TCP receiver | Wrap32 seqno conversion + receiver (ackno, window) | **29/29** |
| 3 — TCP sender | Windowed sending, retransmission, backoff, SYN/FIN | **36/36** (full TCP stack) |
| 4 — Network interface | IP-over-Ethernet with ARP resolution + caching | net_interface **pass** |
| 5 — IP router | Longest-prefix-match forwarding, TTL decrement | net_interface + router **3/3** |

**Speed benchmarks** (optimized build): ByteStream **2.33 Gbit/s**, Reassembler **7.13 Gbit/s** — both well above the course thresholds.

The single test not counted above is `t_webget`, which fetches
`http://cs144.keithw.org/nph-hasher/xyzzy` over the public Internet. That host is
unreachable from this build machine (a plain `curl` to it also times out, while
`curl example.com` returns HTTP 200 in <1s), so the failure is a network-reachability
limitation, not a code defect. The `webget` program itself is verified correct against
`http://example.com/` — see [`results/check0_webget.txt`](results/check0_webget.txt).

## Implemented assignments

- [x] **Checkpoint 0** — Reliable byte stream (`Writer`/`Reader` over a capacity-bounded buffer) + `webget` HTTP client
- [x] **Checkpoint 1** — Stream reassembler (out-of-order, overlapping, capacity-limited)
- [x] **Checkpoint 2** — `Wrap32` sequence-number wrapping/unwrapping + TCP receiver
- [x] **Checkpoint 3** — TCP sender (window filling, retransmission timer, exponential backoff, zero-window probing)
- [x] **Checkpoint 4** — Network interface: IP datagrams over Ethernet frames with ARP
- [x] **Checkpoint 5** — IP router: longest-prefix-match forwarding between interfaces

## Project structure

```
cs144-tcp/
├── src/                 # the implementations (this is the work)
│   ├── byte_stream.{hh,cc}          # check0
│   ├── reassembler.{hh,cc}          # check1
│   ├── wrapping_integers.{hh,cc}    # check2
│   ├── tcp_receiver.{hh,cc}         # check2
│   ├── tcp_sender.{hh,cc}           # check3
│   ├── network_interface.{hh,cc}    # check4
│   └── router.{hh,cc}               # check5
├── apps/webget.cc       # check0 HTTP client
├── tests/               # the course's own test suite (ctest)
├── util/                # provided helpers (sockets, parsers, headers)
├── results/             # captured ctest output + measured numbers
└── CMakeLists.txt
```

## How to run

Requires a Linux toolchain: g++ (C++20), cmake ≥ 3.24. On this machine it was built
in WSL2 Ubuntu 24.04.

```bash
# configure and build
cmake -S . -B build
cmake --build build -j

# run the full test suite (each test runs sanitized + optimized)
cmake --build build --target test          # or: ctest --test-dir build

# run a single checkpoint's tests
cmake --build build --target check0        # byte stream + webget
cmake --build build --target check1        # + reassembler
cmake --build build --target check2        # + receiver
cmake --build build --target check3        # + sender  (full TCP)
cmake --build build --target check4        # network interface
cmake --build build --target check5        # + router

# speed benchmarks
cmake --build build --target speed
```

> Note on the environment: `/mnt/<drive>` (9p) is very slow for CMake; this repo was
> built on a native ext4 path and the git tree kept on the mounted drive.

## Verification

All verification uses the course's shipped `ctest` suite. Captured output lives in
[`results/`](results/):

- [`results/full_suite.txt`](results/full_suite.txt) — 38/38 pass + per-checkpoint breakdown + throughput
- `results/check0_byte_stream.txt` … `results/check5_router.txt` — per-checkpoint runs
- [`results/check0_webget.txt`](results/check0_webget.txt) — webget correctness proof + the GFW note

## Tech stack

C++20, CMake, CTest, AddressSanitizer/UBSan. No third-party libraries — only the C++
standard library and the course-provided socket/parser utilities.

## Key ideas / what I learned

- **Layering**: each layer exposes a narrow interface (byte stream → reassembler →
  receiver/sender → network interface → router) and is testable in isolation.
- **Sequence numbers**: reconciling 32-bit wrapping seqnos with 64-bit absolute
  stream indices, choosing the unwrap nearest a checkpoint.
- **Reliability**: a single retransmission timer with exponential backoff, and the
  subtlety of *not* backing off during zero-window probes.
- **Flow control**: filling exactly the advertised window, and probing a zero window
  with one byte to avoid deadlock.
- **ARP**: on-demand address resolution with a request throttle and mapping expiry.
- **Routing**: longest-prefix match, TTL handling, and checksum recomputation.

## Credits & license

Based on the assignments of **Stanford CS144 (Introduction to Computer Networking)**
by Keith Winstein and the CS144 course staff — the `minnow` framework and its test
suite are theirs (<https://cs144.github.io>). This repository is an independent
educational reimplementation of the student portions; all course materials,
frameworks, and specifications belong to their original authors. My own
implementation code (the bodies I wrote under `src/` and `apps/webget.cc`) is
released under the [MIT License](LICENSE).
