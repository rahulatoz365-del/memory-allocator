# Custom Memory Allocator in C

![Language](https://img.shields.io/badge/Language-C23%2FPOSIX-00599C?style=flat-square&logo=c&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL-FCC624?style=flat-square&logo=linux&logoColor=black)
![Build](https://img.shields.io/badge/Compiler-GCC%20%7C%20Clang-success?style=flat-square)

A thread-safe, POSIX-compliant dynamic memory allocator written in modern **C23**. Built to explore low-level heap management under the hood, it implements `malloc()`, `free()`, `calloc()`, and `realloc()` from scratch using the `sbrk()` system call, an intrusive singly-linked free list, `max_align_t` memory alignment, and `pthread_mutex_t` synchronization. It can be linked directly into test binaries or injected into real Linux utilities (such as `ls`) via `LD_PRELOAD`.

## Table of Contents

1. [Architecture & Memory Layout](#architecture--memory-layout)
2. [Core OS Concepts Under the Hood](#core-os-concepts-under-the-hood)
3. [Codebase & Function Reference](#codebase--function-reference)
4. [Getting Started](#getting-started)
5. [Usage & Sample Output](#usage--sample-output)
6. [Current Limitations & Future Roadmap](#current-limitations--future-roadmap)
7. [Acknowledgments](#acknowledgments)

## Architecture & Memory Layout

Every allocation requested from the OS via `sbrk()` is split into two contiguous regions: an internal **metadata header** (`header_t`) hidden from the caller, followed immediately by the **payload** returned to the application.

```text
  head                                            tail          program break
   v                                               v                 v
+-------------------+----------+   +-------------------+----------+
| header_t (32 B)   | Payload  |-->| header_t (32 B)   | Payload  |--> NULL
| size, is_free,    | (size B) |   | size, is_free,    | (size B) |
| next              |          |   | next              |          |
+-------------------+----------+   +-------------------+----------+
^                   ^
header              (void *)(header + 1)  <-- Pointer returned to caller
```

### Allocation & Deallocation Flow

```text
+-----------------------------------------------------------------------+
|                           malloc(size)                                |
+-----------------------------------------------------------------------+
                                  |
                                  v
                     [ Acquire global mutex ]
                                  |
                                  v
                 +--------------------------------+
                 |     get_free_block(size)       |
                 |  (First-fit list traversal)    |
                 +--------------------------------+
                         |                |
             (Found)     |                |     (Not Found)
                         v                v
          +---------------------+  +------------------------------+
          | Mark is_free = false|  | sbrk(sizeof(header_t) + size)|
          | Release mutex       |  | Init header, append to tail  |
          | Return (header + 1) |  | Release mutex                |
          +---------------------+  | Return (header + 1)          |
                                   +------------------------------+
```

## Core OS Concepts Under the Hood

### 1. Process Virtual Memory & the Program Break (`sbrk`)
A Linux process's virtual address space is divided into **Text**, **Data**, **BSS**, **Heap**, and **Stack** segments. The **program break (`brk`)** marks the current end of the heap.
- `sbrk(0)` returns the current address of the program break without modifying it.
- `sbrk(+n)` increments the program break by `n` bytes, extending the heap.
- `sbrk(-n)` decrements the program break by `n` bytes, releasing memory back to the OS.
- On failure (e.g., out of memory), `sbrk()` returns `(void *)-1`.

### 2. Freeing vs. Releasing Memory
Because the heap managed by `sbrk()` is a single contiguous region, memory can only be returned to the OS from the **very end** (LIFO order). The allocator therefore distinguishes between two cases in `free()`:
- **End-of-Heap Block**: If `(char *)block + header->size == sbrk(0)`, the block sits at the top of the heap. The allocator unlinks it from the tail of the list and shrinks the heap with `sbrk(-(sizeof(header_t) + header->size))`.
- **Interior Block**: If the block is in the middle of the heap, it cannot be returned to the OS without wiping out subsequent allocations. Instead, `header->is_free` is set to `true` so a future `malloc()` call can reuse it via first-fit search.

### 3. Modern C23 Alignment & Symbol Isolation
This implementation improves on the classic tutorial design in several key ways:
- **True Hardware Alignment (`alignas(max_align_t)`)**: Uses C23's native `alignas(max_align_t)` on the header struct and verifies it at compile time with `static_assert(sizeof(header_t) % alignof(max_align_t) == 0)`. This guarantees `header_t` is padded to a 32-byte boundary on x86-64 rather than relying on a non-aligned `char[16]` union stub.
- **Symbol Encapsulation (`static`)**: Internal state (`head`, `tail`, `lock`) and helpers (`get_free_block`) are declared `static` so they do not pollute the global symbol table or collide with host program symbols when loaded via `LD_PRELOAD`.
- **Thread Safety (`pthread_mutex_t`)**: All heap mutations are guarded by a statically initialized global mutex (`PTHREAD_MUTEX_INITIALIZER`). `calloc()` and `realloc()` delegate locking to `malloc()` and `free()` to avoid self-deadlocks.

## Codebase & Function Reference

| Function / Type | Return Type | Parameters | Responsibility |
| :--- | :--- | :--- | :--- |
| `header_t` | `struct` | `size`, `is_free`, `next` | 32-byte aligned metadata block prepended to every allocation. |
| `get_free_block` | `header_t *` | `size_t size` | Traverses the linked list (first-fit) for a reusable free block. |
| `malloc` | `void *` | `size_t size` | Reuses a free block or extends the heap via `sbrk()`. |
| `free` | `void` | `void *block` | Shrinks the heap if at the program break, otherwise marks block free. |
| `calloc` | `void *` | `size_t number, size_t size` | Checks for multiplication overflow, allocates, and zeroes memory. |
| `realloc` | `void *` | `void *ptr, size_t size` | Resizes an existing block, relocating data via `memcpy()` if growing. |
| `print_mem_list` | `void` | `void` | Debug helper that prints `head`, `tail`, and every block's metadata. |

### Detailed Function Breakdown

- **`void *malloc(size_t size)`**
  Returns `NULL` if `size == 0`. Acquires the global mutex and searches for a reusable block via `get_free_block(size)`. If found, marks `is_free = false`, releases the lock, and returns `(void *)(header + 1)`. Otherwise, requests `sizeof(header_t) + size` bytes from `sbrk()`, initializes the header, appends it to the linked list (`head`/`tail`), unlocks, and returns `(void *)(header + 1)`.

- **`void free(void *block)`**
  Ignores `NULL` pointers. Acquires the lock, steps backward one header (`(header_t *)block - 1`), and compares the block's end address against `sbrk(0)`. If it touches the program break, removes the tail node and calls `sbrk(-total_size)`. Otherwise, sets `header->is_free = true`. Releases the lock on every exit path.

- **`void *calloc(size_t number, size_t size)`**
  Returns `NULL` if either argument is `0`. Computes `total_size = number * size` and guards against integer overflow via `size != total_size / number`. Calls `malloc(total_size)` and zeroes the payload with `memset(block, 0, total_size)`.

- **`void *realloc(void *ptr, size_t size)`**
  Delegates to `malloc(size)` if `ptr == NULL`, or calls `free(ptr)` and returns `NULL` if `size == 0`. If the existing block already satisfies `header->size >= size`, returns `ptr` unchanged. Otherwise, allocates a new block with `malloc(size)`, copies the old bytes via `memcpy()`, frees `ptr`, and returns the new pointer.

## Getting Started

### Prerequisites
- Linux or Windows Subsystem for Linux (WSL).
- `gcc` (with `-std=c23` on GCC 14+ or `-std=c2x` on GCC 13) or `clang`.

### Option 1: Standalone Test Binary

Compile `allocator.c` alongside `test.c` and run directly:

```bash
gcc -std=c2x -Wall -Wextra -pthread allocator.c test.c -o test
./test
```

### Option 2: Shared Library & `LD_PRELOAD` Injection

Compile the allocator as a position-independent shared object (`.so`):

```bash
gcc -std=c2x -Wall -Wextra -pthread -fPIC -shared allocator.c -o allocator.so
```

Inject it into a single Linux command without affecting your shell session:

```bash
LD_PRELOAD=$PWD/allocator.so ls
```

## Usage & Sample Output

### Standalone Test (`./test`)

Allocating with `malloc` and `calloc`, freeing an interior block, and growing an allocation with `realloc` (note the 1024-byte block allocated internally by `printf()`'s output buffer):

```text
$ ./test
a[0]=42, b[0]=0
head = 0x59f830293000, tail = 0x59f830293064
addr = 0x59f830293000, size = 16, is_free=0, next=0x59f830293030
addr = 0x59f830293030, size = 20, is_free=0, next=0x59f830293064
addr = 0x59f830293064, size = 1024, is_free=0, next=(nil)

After freeing a:
head = 0x59f830293000, tail = 0x59f830293064
addr = 0x59f830293000, size = 16, is_free=1, next=0x59f830293030
addr = 0x59f830293030, size = 20, is_free=0, next=0x59f830293064
addr = 0x59f830293064, size = 1024, is_free=0, next=(nil)

After reallocating b:
head = 0x59f830293000, tail = 0x59f830293484
addr = 0x59f830293000, size = 16, is_free=1, next=0x59f830293030
addr = 0x59f830293030, size = 20, is_free=1, next=0x59f830293064
addr = 0x59f830293064, size = 1024, is_free=0, next=0x59f830293484
addr = 0x59f830293484, size = 80, is_free=0, next=(nil)
```

### Intercepting `ls` via `LD_PRELOAD`

```text
$ LD_PRELOAD=$PWD/allocator.so ls
README.md  allocator.c  allocator.so  test  test.c
```

## Current Limitations & Future Roadmap

Because this allocator prioritizes clarity and educational value over production throughput, several optimizations are intentionally left for future iterations:

- **Block Coalescing**: Adjacent free blocks are not merged, which can cause external fragmentation over long-running workloads.
- **Block Splitting**: Reusing a large free block for a much smaller request currently leaves the excess bytes unused until the block is freed again.
- **Payload Size Rounding**: Rounding requested sizes up to multiples of `alignof(max_align_t)` ensures every subsequent block header and payload remains strictly 16-byte aligned for SIMD/SSE instructions.
- **`mmap()` for Large Allocations**: Production allocators (such as glibc's `ptmalloc` and `jemalloc`) use `mmap()`/`munmap()` for large allocations so memory can be returned to the OS regardless of heap position.
- **Doubly-Linked Free List & Per-Thread Arenas**: Maintaining a dedicated doubly-linked free list enables $O(1)$ unlinking, while per-thread arenas reduce global mutex contention under heavy multi-threading.

## Acknowledgments

- **Author**: Rahul Singh
- **Reference & Inspiration**: Based on Arjun Sreedharan's systems programming guide, [*Memory Allocators 101 - Write a simple memory allocator*](https://arjunsreedharan.org/post/148675821737/memory-allocators-101-write-a-simple-memory).