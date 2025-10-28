#include "pagetable.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>

// Allocate a new level
Level *allocateLevel(unsigned int depth,
                   unsigned int entryCount)
{
    Level *lvl = new Level;
    lvl->depth = depth;
    lvl->entryCount = entryCount;
    lvl->nextLevelArray = nullptr;
    lvl->mapArray = nullptr;
    return lvl;
}

// Create a new page table
PageTable *createPageTable(unsigned int levelCount,
                          const unsigned int levelBitsArray[])
{
    PageTable *pt = new PageTable;
    pt->levelCount = levelCount;

    // Allocate arrays
    pt->levelBits  = new unsigned int[levelCount];
    pt->levelMask  = new unsigned int[levelCount];
    pt->levelShift = new unsigned int[levelCount];

    unsigned int sumBits = 0;
    for (unsigned int i = 0; i < levelCount; i++) {
        pt->levelBits[i] = levelBitsArray[i];
        sumBits += levelBitsArray[i];
    }
    pt->offsetBits = 32 - sumBits;
    pt->offsetMask = (pt->offsetBits == 32)
        ? 0xFFFFFFFFu
        : ((1u << pt->offsetBits) - 1u);

    // Calculate level masks and shifts
    unsigned int accumulated = 0;
    for (unsigned int i = 0; i < levelCount; i++) {
        unsigned int hiBits = accumulated + pt->levelBits[i];
        unsigned int shift = 32 - hiBits;
        unsigned int bits = pt->levelBits[i];

        unsigned int mask;
        if (bits == 32) {
            mask = 0xFFFFFFFFu;
        } else {
            mask = ((1u << bits) - 1u) << shift;
        }

        pt->levelShift[i] = shift;
        pt->levelMask[i]  = mask;

        accumulated += bits;
    }

    // Allocate root level (depth 0)
    unsigned int rootEntries = 1u << pt->levelBits[0];
    pt->rootLevel = allocateLevel(0, rootEntries);

    return pt;
}

// Destroy a level
static void destroyLevel(PageTable *pt, Level *lvl)
{
    if (lvl == nullptr) return;

    bool leaf = (lvl->depth == (pt->levelCount - 1));

    if (leaf) {
        // Leaf has mapArray
        if (lvl->mapArray != nullptr) {
            delete [] lvl->mapArray;
            lvl->mapArray = nullptr;
        }
    } else {
        // Interior has nextLevelArray
        if (lvl->nextLevelArray != nullptr) {
            for (unsigned int i = 0; i < lvl->entryCount; i++) {
                destroyLevel(pt, lvl->nextLevelArray[i]);
            }
            delete [] lvl->nextLevelArray;
            lvl->nextLevelArray = nullptr;
        }
    }

    delete lvl;
}

void destroyPageTable(PageTable *pt)
{
    if (!pt) return;
    destroyLevel(pt, pt->rootLevel);

    delete [] pt->levelBits;
    delete [] pt->levelMask;
    delete [] pt->levelShift;
    delete pt;
}

// Core ops

unsigned int extractVPNFromVirtualAddress(
    unsigned int virtualAddress,
    unsigned int mask,
    unsigned int shift)
{
    unsigned int part = (virtualAddress & mask) >> shift;
    return part;
}

// Search for the mapped physical frame number in the page table
Map* searchMappedPfn(PageTable *pageTable, unsigned int virtualAddress)
{
    Level *curr = pageTable->rootLevel;

    for (unsigned int d = 0; d < pageTable->levelCount; d++) {
        unsigned int idx = extractVPNFromVirtualAddress(
            virtualAddress,
            pageTable->levelMask[d],
            pageTable->levelShift[d]
        );

        bool leaf = (d == pageTable->levelCount - 1);

        if (leaf) {
            if (!curr->mapArray) return nullptr;
            Map &m = curr->mapArray[idx];
            if (!m.valid || m.frameNumber < 0) {
                return nullptr;
            }
            return &m;
        } else {
            if (!curr->nextLevelArray) return nullptr;
            Level *next = curr->nextLevelArray[idx];
            if (!next) return nullptr;
            curr = next;
        }
    }
    return nullptr; 
}

void insertMapForVpn2Pfn(PageTable *pageTable,
                         unsigned int virtualAddress,
                         int frameNumber)
{
    Level *curr = pageTable->rootLevel;

    for (unsigned int d = 0; d < pageTable->levelCount; d++) {
        unsigned int idx = extractVPNFromVirtualAddress(
            virtualAddress,
            pageTable->levelMask[d],
            pageTable->levelShift[d]
        );

        bool leaf = (d == pageTable->levelCount - 1);

        if (leaf) {
            // Ensure mapArray exists
            if (!curr->mapArray) {
                curr->mapArray = new Map[curr->entryCount];
                for (unsigned int i = 0; i < curr->entryCount; i++) {
                    curr->mapArray[i].frameNumber = -1;
                    curr->mapArray[i].valid = false;
                }
            }

            curr->mapArray[idx].frameNumber = frameNumber;
            if (frameNumber >= 0) {
                curr->mapArray[idx].valid = true;
            } else {
                // Invalidate mapping - used during eviction
                curr->mapArray[idx].valid = false;
            }
        } else {
            // Walk/allocate interior
            if (!curr->nextLevelArray) {
                curr->nextLevelArray = new Level*[curr->entryCount];
                for (unsigned int i = 0; i < curr->entryCount; i++) {
                    curr->nextLevelArray[i] = nullptr;
                }
            }

            if (!curr->nextLevelArray[idx]) {
                unsigned int childEntries =
                    1u << pageTable->levelBits[d+1];
                Level *child = allocateLevel(d+1, childEntries);
                curr->nextLevelArray[idx] = child;
            }

            curr = curr->nextLevelArray[idx];
        }
    }
}

// Helpers

// Get the full VPN from a virtual address
unsigned int getFullVPN(PageTable *pt, unsigned int virtualAddress)
{
    return (pt->offsetBits == 32)
        ? 0
        : (virtualAddress >> pt->offsetBits);
}

unsigned int getOffsetFromVA(PageTable *pt, unsigned int virtualAddress)
{
    return virtualAddress & pt->offsetMask;
}

unsigned int composePhysicalAddress(PageTable *pt,
                                    int frameNumber,
                                    unsigned int offset)
{
    unsigned int phys = ((unsigned int)frameNumber << pt->offsetBits) | offset;
    return phys;
}

// Count the number of page table entries
static unsigned int countEntriesRecursive(PageTable *pt, Level *lvl)
{
    if (!lvl) return 0;

    bool leaf = (lvl->depth == pt->levelCount - 1);

    unsigned int sum = 0;
    if (leaf) {
        if (lvl->mapArray) {
            for (unsigned int i = 0; i < lvl->entryCount; i++) {
                if (lvl->mapArray[i].valid && lvl->mapArray[i].frameNumber >= 0) {
                    sum += 1;
                }
            }
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
                sum += countEntriesRecursive(pt, lvl->nextLevelArray[i]);
            }
        }
    }
    return sum;
}

// Count the number of page table entries
unsigned int countPageTableEntries(Level *lvl,
                                    bool isLeafLevel,
                                    unsigned int depth,
                                    unsigned int lastDepth)
{
    // This function appears to be unused - the actual counting
    // is done by countEntriesRecursiveLocal in main.cpp
    (void)lvl;
    (void)isLeafLevel;
    (void)depth;
    (void)lastDepth;
    return 0;
}

