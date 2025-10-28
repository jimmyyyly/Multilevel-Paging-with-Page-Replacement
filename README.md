# Page Table Simulation with Aging Replacement Algorithm

A C++ implementation of a multi-level page table simulator with aging-based page replacement policy

## Overview

This program simulates virtual memory management using multi-level page tables and implements the aging algorithm for page replacement. It processes virtual address traces and provides detailed logging of page table operations, hits, misses, and evictions.

## Features

- **Multi-level Page Tables**: Supports configurable page table levels with different bit allocations
- **Aging Replacement Algorithm**: Implements aging-based page replacement with configurable bit intervals
- **Comprehensive Logging**: Multiple output modes for debugging and analysis
- **Frame Management**: Configurable frame limits with infinite frame support
- **Trace Processing**: Processes binary trace files with virtual address sequences

## Project Structure

```
├── main.cpp              # Main simulation loop and argument parsing
├── pagetable.h/.cpp     # Page table data structures and operations
├── replacement.h/.cpp   # Aging replacement algorithm implementation
├── log_helpers.h/.c     # Logging utilities for different output modes
├── vaddr_tracereader.h/.c # Trace file reading functionality
├── Makefile             # Build configuration
├── trace.tr             # Sample trace file
├── trace1.tr            # Additional trace file
└── sample_output.txt    # Expected output examples
```

## Building

### Prerequisites
- GCC/G++ compiler with C++17 support
- Make (optional, can compile manually)

### Compilation

Using Makefile:
```bash
make
```

Manual compilation:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -c main.cpp
g++ -std=c++17 -Wall -Wextra -O2 -c pagetable.cpp
g++ -std=c++17 -Wall -Wextra -O2 -c replacement.cpp
gcc -Wall -Wextra -O2 -c vaddr_tracereader.c
gcc -Wall -Wextra -O2 -c log_helpers.c
g++ -std=c++17 -Wall -Wextra -O2 -o pagingwithpr main.o pagetable.o replacement.o vaddr_tracereader.o log_helpers.o
```

## Usage

### Basic Syntax
```bash
./pagingwithpr [options] <trace_file> <level_bits...>
```

### Arguments
- `<trace_file>`: Binary trace file containing virtual addresses
- `<level_bits...>`: Number of bits for each page table level (must sum to ≤32)

### Options
- `-n <num>`: Limit processing to first N addresses
- `-f <frames>`: Maximum number of frames (default: infinite)
- `-b <interval>`: Bit aging interval (default: 10)
- `-l <mode>`: Logging mode (default: summary)

### Logging Modes
- `summary`: Standard hit/miss statistics
- `bitmasks`: Show level masks and shifts
- `offset`: Show offset calculations
- `va2pa`: Virtual to physical address translation
- `vpn2pfn`: VPN to PFN mapping
- `vpn2pfn_pr`: VPN to PFN with page replacement details
- `vpns_pfn`: VPNs for each level and frame number

## Examples

### Basic Page Table Simulation
```bash
./pagingwithpr trace.tr 4 8 8
```

### Limited Frames with Evictions
```bash
./pagingwithpr -f 40 trace.tr 4 4 10
```

### Detailed Logging with Page Replacement
```bash
./pagingwithpr -n 50 -f 20 -b 10 -l vpn2pfn_pr trace.tr 6 6 8
```

### Many Small Levels
```bash
./pagingwithpr -n 100 trace.tr 2 2 2 2 2 2 2 2 2 2 2 2 2 2
```

### Large Singular Levels
```bash
./pagingwithpr -n 100 trace.tr 16 8
```

## Algorithm Details

### Page Table Structure
- Multi-level page tables with configurable bit allocation per level
- Each level contains either pointers to next level or final mappings
- Supports up to 32 total bits for page table addressing

### Aging Replacement Algorithm
- Maintains 16-bit age counters for each loaded page
- Periodically ages all pages by shifting right and setting MSB if accessed
- Selects victim page with lowest age counter value
- New pages start with MSB set (age = 0x8000)

### Address Translation
- Extracts VPN slices using level-specific masks and shifts
- Composes physical addresses from frame numbers and offsets
- Handles page faults by loading new pages or evicting victims

## Output Format

### Summary Mode
```
Page size: 4096 bytes
Addresses processed: 224449
Page hits: 222566, Misses: 1883, Page Replacements: 0
Page hit percentage: 99.16%, miss percentage: 0.84%
Frames allocated: 1883
Number of page table entries: 95614
```

### VPN2PFN_PR Mode
```
0000041F -> 00000000, pagetable miss
0000041F -> 00000000, pagetable hit
000004A3 -> 00000002, pagetable miss, 00005E78 page (with bitstring 3000) was replaced
```

## Testing

The program includes comprehensive test cases covering:
- Balanced level configurations
- Large singular levels
- Many small levels
- Frame limiting behavior
- Various logging modes
- Page table entry counting accuracy

## Author

**Jimmy Ly**  
Date: October 27, 2025

## License
This project is part of an academic assignment and is intended for educational purposes.
