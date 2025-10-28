// Author: Jimmy Ly
// Date: October 27 2025

#ifndef PAGETABLE_H
#define PAGETABLE_H

#include <cstddef> // for size_t
#include <cstdint> // for uint32_t

struct Map {
    int frameNumber; // frame number of the page (>=0 valid, -1 invalid)
    bool valid;      // true if page is mapped
};

struct Level {
    unsigned int depth;     // which level (0 is root)
    unsigned int entryCount; // number of entries in the level

    Level **nextLevelArray; // array of pointers to the next level
    Map *mapArray; // array of maps
};

struct PageTable {
    unsigned int levelCount;     // N
    unsigned int *levelBits;     // [N] bits for each level
    unsigned int *levelMask;     // [N] mask for extracting that level's VPN slice
    unsigned int *levelShift;    // [N] right shift for that level
    unsigned int offsetBits;     // remaining bits for offset
    unsigned int offsetMask;     // mask for offset
    Level *rootLevel;            // level 0
};

// Extract VPN slice from a virtual address using given mask+shift
unsigned int extractVPNFromVirtualAddress(unsigned int virtualAddress, unsigned int mask, unsigned int shift);

// Search for the mapped physical frame number in the page table
Map* searchMappedPfn(PageTable *pageTable, unsigned int virtualAddress);

// Insert a new map for the virtual address to physical frame number
void insertMapForVpn2Pfn(PageTable *pageTable, unsigned int virtualAddress, int frameNumber);

// Create a new page table
PageTable *createPageTable(unsigned int levelCount, const unsigned int levelBitsArray[]);

// Destroy a page table
void destroyPageTable(PageTable *pt);

// Helpers

// Get the full VPN from a virtual address
unsigned int getFullVPN(PageTable *pt, unsigned int virtualAddress);

// Get the offset from a virtual address
unsigned int getOffsetFromVA(PageTable *pt, unsigned int virtualAddress);

// Compose the physical address from the frame number and offset
unsigned int composePhysicalAddress(PageTable *pt, int frameNumber, unsigned int offset);

// Count the number of page table entries
unsigned int countPageTableEntries(Level *lvl, bool isLeafLevel, unsigned int depth, unsigned int lastDepth);

// Allocate a new level
Level *allocateLevel(unsigned int depth, unsigned int entryCount);

#endif // PAGETABLE_H