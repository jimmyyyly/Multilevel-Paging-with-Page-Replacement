// Author: Jimmy Ly
// Date: October 27 2025

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <climits>
#include <unistd.h>
#include "pagetable.h"
#include "replacement.h"

// Forward declarations for C functions
extern "C" {
    typedef struct {
        unsigned int addr;
        unsigned char reqtype;
        unsigned char size;
        unsigned char attr;
        unsigned char proc;
        unsigned int time;
    } p2AddrTr;

    int NextAddress(FILE *traceFile, p2AddrTr *outAddr);

    void log_summary(unsigned int page_size, 
                     unsigned int numOfPageReplaces,
                     unsigned int pageTableHits, 
                     unsigned int numOfAddresses, 
                     unsigned int numOfFramesAllocated,
                     unsigned long int pgtableEntries);
    void log_bitmasks(int levels, uint32_t *masks);
    void log_va2pa(unsigned int va, unsigned int pa);
    void log_vpns_pfn(int levels, uint32_t *vpns, uint32_t frame);
    void log_vpn2pfn(unsigned int va,
                     const PageTable *pt,
                     int pfn,
                     bool hit);
    void log_vpn2pfn_pr(unsigned int va,
                        const PageTable *pt,
                        int pfn,
                        bool hit,
                        bool didEvict,
                        unsigned int evictedVPN,
                        uint16_t evictedAgeBits,
                        unsigned int offsetBits);
    void print_num_inHex(unsigned int num);
}

// Simulation statistics
struct Stats {
    unsigned int addressesProcessed;
    unsigned int hits;
    unsigned int misses;
    unsigned int evictions;
};

static void initStats(Stats &s) {
    s.addressesProcessed = 0;
    s.hits = 0;
    s.misses = 0;
    s.evictions = 0;
}

// Count the number of page table entries recursively
static unsigned int countEntriesRecursiveLocal(PageTable *pt, Level *lvl)
{
    if (!lvl) return 0;

    bool leaf = (lvl->depth == pt->levelCount - 1);

    unsigned int sum = 0;
    if (leaf) {
        if (lvl->mapArray) {
            // Count all allocated entries in leaf level
            sum = lvl->entryCount;
        }
    } else {
        if (lvl->nextLevelArray) {
            for (unsigned int i = 0; i < lvl->entryCount; i++) {
                if (lvl->nextLevelArray[i] != nullptr) {
                    sum += 1; 
                }
            }
            // Recurse into each child
            for (unsigned int i = 0; i < lvl->entryCount; i++) {
                sum += countEntriesRecursiveLocal(pt, lvl->nextLevelArray[i]);
            }
        }
    }
    return sum;
}

// Main
int main(int argc, char **argv)
{
    unsigned int limitN = 0;            // -n
    unsigned int haveLimitN = 0;
    unsigned int maxFrames = UINT_MAX;    // -f, default means "infinite"
    unsigned int haveF = 0;
    unsigned int bitInterval = 10;      // -b
    unsigned int haveB = 0;
    const char* logMode = "summary";    // -l

    int opt;
    while ( (opt = getopt(argc, argv, "n:f:b:l:")) != -1 ) {
        switch(opt) {
        case 'n':
            limitN = (unsigned int) atoi(optarg);
            if (limitN < 1) {
                fprintf(stderr,
                        "Number of memory accesses must be a number and greater than 0\n");
                return 1;
            }
            haveLimitN = 1;
            break;
        case 'f':
            maxFrames = (unsigned int) atoi(optarg);
            if (maxFrames < 1) {
                fprintf(stderr,
                        "Number of available frames must be a number and greater than 0\n");
                return 1;
            }
            haveF = 1;
            break;
        case 'b':
            bitInterval = (unsigned int) atoi(optarg);
            if (bitInterval < 1) {
                fprintf(stderr,
                        "Bit string update interval must be a number and greater than 0\n");
                return 1;
            }
            haveB = 1;
            break;
        case 'l':
            logMode = optarg;
            break;
        default:
            fprintf(stderr, "Bad argument\n");
            return 1;
        }
    }
    int idx = optind;

    if (idx >= argc) {
        fprintf(stderr, "Missing trace file\n");
        return 1;
    }

    const char* tracePath = argv[idx++];
    FILE *traceFile = fopen(tracePath, "rb");
    if (!traceFile) {
        fprintf(stderr, "Unable to open %s\n", tracePath);
        return 1;
    }

    // Remaining args are level bits
    if (idx >= argc) {
        fprintf(stderr, "Missing level bits\n");
        fclose(traceFile);
        return 1;
    }

    // collect level bits into array
    unsigned int tempBits[32];
    unsigned int levelCount = 0;
    unsigned int sumBits = 0;

    while (idx < argc) {
        unsigned int bits = (unsigned int) atoi(argv[idx]);
        if (bits < 1) {
            fprintf(stderr,
                    "Level %u page table must be at least 1 bit\n",
                    levelCount);
            fclose(traceFile);
            return 1;
        }
        tempBits[levelCount] = bits;
        levelCount++;
        sumBits += bits;
        idx++;
    }

    if (sumBits > 28) {
        fprintf(stderr, "Too many bits used in page tables\n");
        fclose(traceFile);
        return 1;
    }

    // Build page table
    PageTable *pt = createPageTable(levelCount, tempBits);

    // If mode is just "bitmasks", we only print bitmask info then exit
    if (strcmp(logMode, "bitmasks") == 0) {
        // instructor helper
        log_bitmasks(pt->levelCount, pt->levelMask);
        destroyPageTable(pt);
        fclose(traceFile);
        return 0;
    }

    // Initialize replacement state
    ReplacementState rs;
    initReplacementState(rs, maxFrames, bitInterval);

    Stats stats;
    initStats(stats);

    // Main loop: read addresses
    p2AddrTr rec;
    while (1) {
        if (haveLimitN && stats.addressesProcessed >= limitN) {
            break;  // Reached -n
        }

        int ok = NextAddress(traceFile, &rec);
        if (!ok) break; // EOF

        unsigned int va = rec.addr;
        stats.addressesProcessed++;

        // Tick time / maybe aging update
        tickReplacementClock(rs);

        // Look up in page table
        Map *m = searchMappedPfn(pt, va);

        bool hit = (m != nullptr);
        bool didFault = false;
        bool didEvict = false;
        unsigned int evictedVPN = 0;
        uint16_t evictedAgeBits = 0;

        int pfn = -1;

        unsigned int fullVPN = getFullVPN(pt, va);

        if (hit) {
            // Page hit
            pfn = m->frameNumber;
            stats.hits++;
        } else {
            // Miss / demand paging
            stats.misses++;

            // EnsureResidentPage also updates pageTable for us
            pfn = ensureResidentPage(pt,
                                     rs,
                                     va,
                                     fullVPN,
                                     didFault,
                                     didEvict,
                                     evictedVPN,
                                     evictedAgeBits);

            if (didEvict) {
                stats.evictions++;
            }
        }

        // Track access in replacement state
        noteFrameAccess(rs, fullVPN, pfn);

        // Compute PA / offset for logging
        unsigned int offset = getOffsetFromVA(pt, va);
        unsigned int pa = composePhysicalAddress(pt, pfn, offset);

        // Logging per-address depending on logMode
        if (strcmp(logMode, "va2pa") == 0) {
            log_va2pa(va, pa);
        } else if (strcmp(logMode, "offset") == 0) {
            print_num_inHex(offset);
        } else if (strcmp(logMode, "vpn2pfn") == 0) {
            // Simple mapping info, no eviction details
            log_vpn2pfn(va, pt, pfn, hit);
        } else if (strcmp(logMode, "vpn2pfn_pr") == 0) {
            // With page replacement info
            log_vpn2pfn_pr(va,
                           pt,
                           pfn,
                           hit,
                           didEvict,
                           evictedVPN,
                           evictedAgeBits,
                           pt->offsetBits);
        } else if (strcmp(logMode, "vpns_pfn") == 0) {
            // VPNs for each level and frame number
            unsigned int *vpns = new unsigned int[pt->levelCount];
            for (unsigned int i = 0; i < pt->levelCount; i++) {
                vpns[i] = extractVPNFromVirtualAddress(va, pt->levelMask[i], pt->levelShift[i]);
            }
            log_vpns_pfn(pt->levelCount, vpns, pfn);
            delete[] vpns;
        } else {
            // "Summary" mode doesn't log per-line
        }
    }

    // End of trace, produce summary if mode == summary
    if (strcmp(logMode, "summary") == 0) {
        unsigned int pageTableEntries =
            countEntriesRecursiveLocal(pt, pt->rootLevel);
        
        // Calculate page size from offset bits
        unsigned int pageSize = 1u << pt->offsetBits;
        
        // Call log_summary with proper parameters
        log_summary(pageSize, stats.evictions, stats.hits, 
                   stats.addressesProcessed, rs.nextFreeFrame, pageTableEntries);
    }

    destroyPageTable(pt);
    fclose(traceFile);
    return 0;
}