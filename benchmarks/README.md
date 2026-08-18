# Lua xjson Benchmarks

## Implementation Notes

Lua xjson is designed with performance as a primary objective. Its implementation includes:

* Direct conversion between JSON text and Lua values without an intermediate representation.
* Custom integer and floating-point conversion paths using code adapted from
  [yyjson](https://github.com/ibireme/yyjson).
* Array and object selection based on raw table length, adapted from David Kolf's
  [array detection proposal](https://dkolf.de/dkjson-lua/roadmap-3.0#array-detection) for dkjson,
  avoiding a separate key-classification pass for non-empty arrays.
* Speculative table allocation for sibling containers during decoding (with an optional incremental
  allocation mode).
* Speculative memory allocation for escaped strings during memory encoding (with an optional
  incremental allocation mode).
* Separate processing paths for escaped and unescaped strings with bulk copies of contiguous spans.
* Compiler branch hints on terminal error paths.
* Direct and buffered file output.
* Lua 5.4 to-be-closed variables for efficient memory management.
* Lua 5.5 external strings to return encoded documents without a final copy.


## Methodology

The present benchmarks compare Lua xjson 0.9.0 beta with
[Lua CJSON 2.1devel-1](https://luarocks.org/modules/criztianix/lua-cjson2/2.1devel-1) and
[dkjson 2.11-1](https://luarocks.org/modules/dhkolf/dkjson/2.11-1). The libraries are configured to
align accepted input, Lua representations, and encoded output behavior as closely as their
interfaces permit. The benchmarks measure each library's normal implementation of the same
externally visible operations; they do not attempt to equalize internal algorithms or instruction
counts.

Specifically:

- Lua xjson 0.9.0 beta encodes with the `s` flag to match Lua CJSON's escaping of forward slashes.
- Lua CJSON is configured to reject invalid numbers and encode empty tables as arrays to match Lua
  xjson.
- dkjson uses its optional LPeg decoder. Object and array metatables are disabled during decoding to
  match the Lua representations produced by Lua xjson and Lua CJSON; empty tables are therefore also
  encoded as arrays. dkjson does not escape forward slashes.

To interpret the encoding results, it is useful to separate encoding into two parts:
*classification*, which decides whether a Lua table represents a JSON array or object, and
*serialization*, which writes that table's contents as JSON. Lua xjson classifies a table from its
raw length and, when that length is zero, a single `lua_next` probe. Lua CJSON 2.1devel-1 and dkjson
2.11 instead inspect table keys to determine whether a table is an array. This inspection can stop
early, but in the worst case scans the complete table, as it typically does for dense arrays. Each
value measured for encoding is produced by decoding a valid JSON benchmark document, so the inputs
contain neither mixed nor sparse Lua tables: arrays have consecutive integer keys and objects have
string keys. All three libraries are also configured to encode empty tables as arrays. As a result,
corresponding tables are classified consistently, and after classification all three libraries
serialize the same table contents. The implementation difference is therefore limited to the
classification work.

The libraries address different use cases and make different implementation trade-offs. Lua CJSON
was selected because its implementation in C and focus on performance make it a natural compiled
comparison. dkjson was selected for the portability of its pure-Lua implementation. Performance is
one of several considerations when selecting a JSON library, alongside its capabilities,
table-classification behavior, portability, dependencies, memory footprint, and other implementation
trade-offs.

The full set of ten JSON documents in the yyjson_benchmark corpus is exercised to represent a broad
range of JSON workloads.

The repetition counts were selected to produce broadly similar Lua xjson decoding times for each
document.

Each JSON document is loaded into memory before measurement. Decoding repeatedly processes the
original document text. Encoding first decodes the document with the library under measurement and
then repeatedly encodes the resulting Lua value. Module initialization, document loading, and
preparatory decoding are outside the measured interval. CPU time is measured with `os.clock`.

The averages use the nine documents completed by all three libraries and give each document equal
weight.


## Reproduction

The benchmarks use the JSON corpus from
[yyjson_benchmark](https://github.com/ibireme/yyjson_benchmark). To run the benchmarks, clone the
corpus at the tested revision, build Lua xjson, and invoke the benchmark runner:

```sh
git clone https://github.com/ibireme/yyjson_benchmark.git benchmarks/yyjson_benchmark
git -C benchmarks/yyjson_benchmark checkout aeefe6a44f37fccf1f9d730766abab9ffea43c6b
make clean all LUA_ABI=5.4
benchmarks/run 5.4
```

> [!NOTE]
> When intending to benchmark the locally compiled Lua xjson, verify that it is not shadowed by an
> installed copy appearing earlier in the C path (`package.cpath`).


## Measurements

### Apple M4 Max

The following measurements were obtained on a Mac Studio with a 16-core Apple M4 Max processor and
128 GB of memory. The host operating system was macOS Tahoe 26.5.2. Docker hosted an Ubuntu 26.04
development container built for ARM64. The benchmarks used Lua 5.4.8 within that container. dkjson
decoding used LPeg 1.1.0.

```
┌─────────┬───────────┬────────────────┬─────────────┬────────────────┐
│ Library │ Operation │ Document       │ Repetitions │            CPU │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ canada         │         800 │        5.120 s │
│ cjson   │ decode    │ canada         │         800 │       11.130 s │
│ dkjson  │ decode    │ canada         │         800 │       63.263 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ canada         │         800 │        2.459 s │
│ cjson   │ encode    │ canada         │         800 │       19.584 s │
│ dkjson  │ encode    │ canada         │         800 │       67.283 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ citm_catalog   │        1800 │        4.970 s │
│ cjson   │ decode    │ citm_catalog   │        1800 │        6.356 s │
│ dkjson  │ decode    │ citm_catalog   │        1800 │       45.087 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ citm_catalog   │        1800 │        2.257 s │
│ cjson   │ encode    │ citm_catalog   │        1800 │        5.381 s │
│ dkjson  │ encode    │ citm_catalog   │        1800 │       35.329 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ fgo            │          25 │        5.272 s │
│ cjson   │ decode    │ fgo            │          25 │        7.581 s │
│ dkjson  │ decode    │ fgo            │          25 │       39.751 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ fgo            │          25 │        2.800 s │
│ cjson   │ encode    │ fgo            │          25 │        8.575 s │
│ dkjson  │ encode    │ fgo            │          25 │       49.402 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ github_events  │       40000 │        4.921 s │
│ cjson   │ decode    │ github_events  │       40000 │        5.171 s │
│ dkjson  │ decode    │ github_events  │       40000 │              - │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ github_events  │       40000 │        3.062 s │
│ cjson   │ encode    │ github_events  │       40000 │        2.955 s │
│ dkjson  │ encode    │ github_events  │       40000 │              - │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ gsoc-2018      │        1200 │        4.556 s │
│ cjson   │ decode    │ gsoc-2018      │        1200 │        4.354 s │
│ dkjson  │ decode    │ gsoc-2018      │        1200 │       59.199 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ gsoc-2018      │        1200 │        3.493 s │
│ cjson   │ encode    │ gsoc-2018      │        1200 │        7.812 s │
│ dkjson  │ encode    │ gsoc-2018      │        1200 │       89.696 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ lottie         │        1800 │        4.800 s │
│ cjson   │ decode    │ lottie         │        1800 │        6.110 s │
│ dkjson  │ decode    │ lottie         │        1800 │       29.988 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ lottie         │        1800 │        2.159 s │
│ cjson   │ encode    │ lottie         │        1800 │        6.986 s │
│ dkjson  │ encode    │ lottie         │        1800 │       34.064 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ otfcc          │          10 │        5.075 s │
│ cjson   │ decode    │ otfcc          │          10 │        7.216 s │
│ dkjson  │ decode    │ otfcc          │          10 │       30.445 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ otfcc          │          10 │        3.158 s │
│ cjson   │ encode    │ otfcc          │          10 │        7.069 s │
│ dkjson  │ encode    │ otfcc          │          10 │       33.941 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ poet           │        1200 │        5.096 s │
│ cjson   │ decode    │ poet           │        1200 │       11.434 s │
│ dkjson  │ decode    │ poet           │        1200 │       56.651 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ poet           │        1200 │        3.401 s │
│ cjson   │ encode    │ poet           │        1200 │        8.564 s │
│ dkjson  │ encode    │ poet           │        1200 │      121.804 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ twitterescaped │        4500 │        4.846 s │
│ cjson   │ decode    │ twitterescaped │        4500 │        7.329 s │
│ dkjson  │ decode    │ twitterescaped │        4500 │       90.722 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ twitterescaped │        4500 │        3.043 s │
│ cjson   │ encode    │ twitterescaped │        4500 │        3.386 s │
│ dkjson  │ encode    │ twitterescaped │        4500 │       60.635 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ twitter        │        4500 │        5.054 s │
│ cjson   │ decode    │ twitter        │        4500 │        7.074 s │
│ dkjson  │ decode    │ twitter        │        4500 │       47.988 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ twitter        │        4500 │        2.928 s │
│ cjson   │ encode    │ twitter        │        4500 │        3.329 s │
│ dkjson  │ encode    │ twitter        │        4500 │       59.441 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ average        │           - │        4.977 s │
│ cjson   │ decode    │ average        │           - │        7.620 s │
│ dkjson  │ decode    │ average        │           - │       51.455 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ average        │           - │        2.855 s │
│ cjson   │ encode    │ average        │           - │        7.854 s │
│ dkjson  │ encode    │ average        │           - │       61.288 s │
└─────────┴───────────┴────────────────┴─────────────┴────────────────┘
```


### AMD EPYC

The following measurements were obtained on a KVM virtual machine configured with six AMD EPYC vCPUs
and 32 GB of memory. The guest operating system was Ubuntu 24.04.4 LTS for x86-64. The benchmarks
used Lua 5.4.6 within that virtual machine. dkjson decoding used LPeg 1.0.2.

```
┌─────────┬───────────┬────────────────┬─────────────┬────────────────┐
│ Library │ Operation │ Document       │ Repetitions │            CPU │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ canada         │         800 │       13.569 s │
│ cjson   │ decode    │ canada         │         800 │       37.422 s │
│ dkjson  │ decode    │ canada         │         800 │      203.808 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ canada         │         800 │        6.387 s │
│ cjson   │ encode    │ canada         │         800 │       42.757 s │
│ dkjson  │ encode    │ canada         │         800 │      208.596 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ citm_catalog   │        1800 │       14.746 s │
│ cjson   │ decode    │ citm_catalog   │        1800 │       23.319 s │
│ dkjson  │ decode    │ citm_catalog   │        1800 │      128.696 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ citm_catalog   │        1800 │        6.469 s │
│ cjson   │ encode    │ citm_catalog   │        1800 │       15.264 s │
│ dkjson  │ encode    │ citm_catalog   │        1800 │      105.803 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ fgo            │          25 │       15.566 s │
│ cjson   │ decode    │ fgo            │          25 │       26.642 s │
│ dkjson  │ decode    │ fgo            │          25 │      110.425 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ fgo            │          25 │        9.806 s │
│ cjson   │ encode    │ fgo            │          25 │       25.385 s │
│ dkjson  │ encode    │ fgo            │          25 │      160.256 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ github_events  │       40000 │       14.168 s │
│ cjson   │ decode    │ github_events  │       40000 │       19.460 s │
│ dkjson  │ decode    │ github_events  │       40000 │              - │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ github_events  │       40000 │        8.193 s │
│ cjson   │ encode    │ github_events  │       40000 │       10.679 s │
│ dkjson  │ encode    │ github_events  │       40000 │              - │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ gsoc-2018      │        1200 │       12.088 s │
│ cjson   │ decode    │ gsoc-2018      │        1200 │       18.024 s │
│ dkjson  │ decode    │ gsoc-2018      │        1200 │      139.397 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ gsoc-2018      │        1200 │        8.900 s │
│ cjson   │ encode    │ gsoc-2018      │        1200 │       12.143 s │
│ dkjson  │ encode    │ gsoc-2018      │        1200 │      278.283 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ lottie         │        1800 │       15.474 s │
│ cjson   │ decode    │ lottie         │        1800 │       22.939 s │
│ dkjson  │ decode    │ lottie         │        1800 │       89.147 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ lottie         │        1800 │        6.351 s │
│ cjson   │ encode    │ lottie         │        1800 │       18.755 s │
│ dkjson  │ encode    │ lottie         │        1800 │      115.043 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ otfcc          │          10 │       17.084 s │
│ cjson   │ decode    │ otfcc          │          10 │       30.002 s │
│ dkjson  │ decode    │ otfcc          │          10 │       86.922 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ otfcc          │          10 │        8.835 s │
│ cjson   │ encode    │ otfcc          │          10 │       17.661 s │
│ dkjson  │ encode    │ otfcc          │          10 │      109.799 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ poet           │        1200 │       14.349 s │
│ cjson   │ decode    │ poet           │        1200 │       25.228 s │
│ dkjson  │ decode    │ poet           │        1200 │      156.063 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ poet           │        1200 │        9.839 s │
│ cjson   │ encode    │ poet           │        1200 │       13.552 s │
│ dkjson  │ encode    │ poet           │        1200 │      353.102 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ twitter        │        4500 │       15.277 s │
│ cjson   │ decode    │ twitter        │        4500 │       20.684 s │
│ dkjson  │ decode    │ twitter        │        4500 │      125.830 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ twitter        │        4500 │        8.726 s │
│ cjson   │ encode    │ twitter        │        4500 │       11.720 s │
│ dkjson  │ encode    │ twitter        │        4500 │      185.505 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ twitterescaped │        4500 │       14.554 s │
│ cjson   │ decode    │ twitterescaped │        4500 │       20.945 s │
│ dkjson  │ decode    │ twitterescaped │        4500 │      232.780 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ twitterescaped │        4500 │        8.874 s │
│ cjson   │ encode    │ twitterescaped │        4500 │       11.604 s │
│ dkjson  │ encode    │ twitterescaped │        4500 │      186.010 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ decode    │ average        │           - │       14.745 s │
│ cjson   │ decode    │ average        │           - │       25.023 s │
│ dkjson  │ decode    │ average        │           - │      141.452 s │
├─────────┼───────────┼────────────────┼─────────────┼────────────────┤
│ xjson   │ encode    │ average        │           - │        8.243 s │
│ cjson   │ encode    │ average        │           - │       18.760 s │
│ dkjson  │ encode    │ average        │           - │      189.155 s │
└─────────┴───────────┴────────────────┴─────────────┴────────────────┘
```


### Other Environments

Absolute CPU times depend on the execution environment. If you encounter materially different
relative results in a particular execution environment, please report them.
