#ifndef REPLACEMENT_H
#define REPLACEMENT_H

#include <vector>
#include <cstdint>
#include "pagetable.h"

// Forward declaration to avoid circular dependency
struct PageTable;

// Struct to store information about a loaded page
struct LoadedPageInfo {
    unsigned int fullVPN;           
    int frameNumber;                
    uint16_t ageBits;               
    unsigned int lastAccessTime;    
    bool accessedThisInterval;      
};

// Struct to store replacement state
struct ReplacementState {
    unsigned int maxFrames;           
    unsigned int bitstringInterval;   
    unsigned int accessesSinceAging;  
    unsigned int currentTime;         
    unsigned int nextFreeFrame;       

    std::vector<LoadedPageInfo> loaded;
};

// Initialize the replacement state
void initReplacementState(ReplacementState &rs,
    unsigned int maxFrames,
    unsigned int bitInterval);

// Tick the replacement clock
void tickReplacementClock(ReplacementState &rs);

// Note a frame access
void noteFrameAccess(ReplacementState &rs, unsigned int fullVPN, int frameNumber);

// Find a loaded VPN
int findLoadedVPN(const ReplacementState &rs, unsigned int fullVPN);

// Ensure a resident page
int ensureResidentPage(PageTable *pt,
    ReplacementState &rs,
    unsigned int virtualAddress,
    unsigned int fullVPN,
    bool &didFault,
    bool &didEvict,
    unsigned int &evictedVPN,
    uint16_t &evictedAgeBits);

// Perform aging update
void performAgingUpdate(ReplacementState &rs);

#endif // REPLACEMENT_H
