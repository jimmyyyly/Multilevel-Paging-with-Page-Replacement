#include "replacement.h"
#include <limits>

// Initialize the replacement state
void initReplacementState(ReplacementState &rs,
                          unsigned int maxFrames,
                          unsigned int bitInterval)
{
    rs.maxFrames = maxFrames;
    rs.bitstringInterval = bitInterval;
    rs.accessesSinceAging = 0;
    rs.currentTime = 0;
    rs.nextFreeFrame = 0;
    rs.loaded.clear();
}

int findLoadedVPN(const ReplacementState &rs, unsigned int fullVPN)
{
    for (size_t i = 0; i < rs.loaded.size(); i++) {
        if (rs.loaded[i].fullVPN == fullVPN) {
            return (int)i;
        }
    }
    return -1;
}

// Perform aging update
void performAgingUpdate(ReplacementState &rs)
{
    for (auto &entry : rs.loaded) {
        // Shift right by 1
        entry.ageBits >>= 1;
        
        // If it was accessed within the last interval, set MSB = 1
        if (entry.accessedThisInterval) {
            entry.ageBits |= (1u << 15);
        }

        // Reset flag
        entry.accessedThisInterval = false;
    }
}

// Tick the replacement clock
void tickReplacementClock(ReplacementState &rs)
{
    rs.currentTime += 1;
    rs.accessesSinceAging += 1;

    if (rs.accessesSinceAging >= rs.bitstringInterval) {
        // Time to age
        performAgingUpdate(rs);
        rs.accessesSinceAging = 0;
    }
}

// Note a frame access
void noteFrameAccess(ReplacementState &rs,
                    unsigned int fullVPN,
                    int frameNumber)
{
    for (auto &entry : rs.loaded) {
        if (entry.fullVPN == fullVPN &&
            entry.frameNumber == frameNumber)
        {
            entry.lastAccessTime = rs.currentTime;
            entry.accessedThisInterval = true;
            return;
        }
    }
}

// Choose a victim index
static int chooseVictimIndex(const ReplacementState &rs)
{
    // If no loaded pages, return -1
    if (rs.loaded.empty()) return -1;

    uint16_t bestAge = std::numeric_limits<uint16_t>::max();
    unsigned int bestLast = std::numeric_limits<unsigned int>::max();
    int bestIdx = -1;

    for (size_t i = 0; i < rs.loaded.size(); i++) {
        const auto &e = rs.loaded[i];
        if ( (e.ageBits < bestAge) ||
             (e.ageBits == bestAge && e.lastAccessTime < bestLast) )
        {
            bestAge = e.ageBits;
            bestLast = e.lastAccessTime;
            bestIdx = (int)i;
        }
    }
    return bestIdx;
}

int ensureResidentPage(PageTable *pt,
                        ReplacementState &rs,
                        unsigned int virtualAddress,
                        unsigned int fullVPN,
                        bool &didFault,
                        bool &didEvict,
                        unsigned int &evictedVPN,
                        uint16_t &evictedAgeBits)
{
    didFault = false;
    didEvict = false;
    evictedVPN = 0;
    evictedAgeBits = 0;

    // Check if the page is already resident
    int idx = findLoadedVPN(rs, fullVPN);
    if (idx >= 0) {
        return rs.loaded[idx].frameNumber;
    }
    didFault = true;
    
    // If there is still space for the new page
    if (rs.loaded.size() < rs.maxFrames) {
        int newPFN = rs.nextFreeFrame;
        rs.nextFreeFrame++;

        // Insert into page table
        insertMapForVpn2Pfn(pt, virtualAddress, newPFN);

        // Track in loaded
        LoadedPageInfo info;
        info.fullVPN = fullVPN;
        info.frameNumber = newPFN;
        info.ageBits = (1u << 15); // new page starts with MSB set
        info.lastAccessTime = rs.currentTime;
        info.accessedThisInterval = true;
        rs.loaded.push_back(info);

        return newPFN;
    }

    // Otherwise: we must evict someone
    int victimIdx = chooseVictimIndex(rs);
    LoadedPageInfo &victim = rs.loaded[victimIdx];

    didEvict = true;
    evictedVPN = victim.fullVPN;
    evictedAgeBits = victim.ageBits;

    int reusedPFN = victim.frameNumber;
    // Get the victim virtual address
    unsigned int victimVA = (evictedVPN << pt->offsetBits);
    insertMapForVpn2Pfn(pt, victimVA, -1);

    // Now reuse victim entry struct for the new page:
    victim.fullVPN = fullVPN;
    victim.frameNumber = reusedPFN;
    victim.ageBits = (1u << 15);
    victim.lastAccessTime = rs.currentTime;
    victim.accessedThisInterval = true;

    // Install new mapping in page table
    insertMapForVpn2Pfn(pt, virtualAddress, reusedPFN);

    return reusedPFN;
}
