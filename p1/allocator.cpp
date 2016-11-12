#include "allocator.hpp"

#include <list>
#include <cstring>

/**
  * @brief Information structure for a memory space.
  */
struct MemoryInfo {
    /** Pointer to the beginning of the memory area. */
    void* pointer;
    /** Memory area size. */
    size_t size;
    /** Whenever the memory area is free or allocated. */
    bool is_freespace;

    /** Simple constructor. */
    MemoryInfo(void* p, size_t s, bool isf):
        pointer(p), size(s), is_freespace(isf)
        { }
};

/**
  * @brief The Pointer implementation structure.
  */
struct Pointer::PointerImpl
{
    /** Allocator reference. If nullptr then the pointer is invalid. */
    Allocator* allocator;
    /** Iterator reference to a memory area. */
    std::list<MemoryInfo>::iterator p;

    /** Pointer implementation constructor. */
    PointerImpl():
        allocator(nullptr) { }
};

/** Pointer constructor. Allocates implementation structure. */
Pointer::Pointer():
    impl(new PointerImpl) { }

/** Copy constructor for the pointer. Necessary to return a pointer from a
  * structure.
  */
Pointer::Pointer(const Pointer &p):
    impl(new PointerImpl)
{
    *this = p;
}

/** Pointer destuctor. Deallocates the pointer implementation. */
Pointer::~Pointer()
{
    // Reset reference iterator
    this->impl->p = std::list<MemoryInfo>::iterator();

    delete this->impl;
}

/** Assignment operator for the pointer. */
Pointer& Pointer::operator=(const Pointer& p)
{
    this->impl->p = p.impl->p;
    this->impl->allocator = p.impl->allocator;

    return *this;
}

/** Pre-defined. Obtains the plain pointer to the memory area.
  * @return Plain pointer to the memory area.
  */
void* Pointer::get() const
{
    if (this->impl->allocator) {
        return this->impl->p->pointer;
    } else {
        return nullptr;
    }
}

/**
  * @brief The Allocator implementation structure.
  */
struct Allocator::AllocatorImpl
{
    /** Memory areas list */
    std::list<MemoryInfo> memory_table;
    /** Unites consecutive free memory areas into one free memory area.
      * @arg fs Iterator to one of consecutive free memory areas.
      * @return Iterator for a united area if success. Invalid iterator if
      * non-free memory area specified.
      */
    std::list<MemoryInfo>::iterator UniteFreeSpace(
        std::list<MemoryInfo>::iterator fs
    );

    /** Swaps the allocated memory area with the previous free memory area.
      * @arg alloc_mem - allocated memory area reference iterator.
      * @return True if successful. False if nothing has been done.
      */
    bool moveToPreviousFree(std::list<MemoryInfo>::iterator alloc_mem);

    /** Extends the allocated memory area to the next free memory area.
      * @arg alloc_mem - allocated memory area reference iterator.
      * @arg new_size - new size of extended memory area.
      * @return True if successful. False if nothing has been done.
      */
    bool extendToNextFree(
        std::list<MemoryInfo>::iterator alloc_mem, size_t new_size
    );
};

/** Calculates memory amount of the consecutive memory areas range
  * [first, last).
  * @arg first - first memory area reference iterator.
  * @arg last - the memory after the last area in the range.
  * @return Memory amount in the range.
  */
size_t rangeMemoryAmount(
    std::list<MemoryInfo>::iterator first,
    std::list<MemoryInfo>::iterator last
)
{
    std::list<MemoryInfo>::iterator current;
    size_t returnValue = 0;

    for (current = first; current != last; ++current) {
        returnValue += current->size;
    }

    return returnValue;
}


bool Allocator::AllocatorImpl::moveToPreviousFree(
    std::list<MemoryInfo>::iterator alloc_mem
)
{
    // Simple checks
    if (alloc_mem == this->memory_table.begin()) {
        return false;
    } else if (alloc_mem == memory_table.end()) {
        return false;
    } else if (alloc_mem->is_freespace) {
        return false;
    }

    // Obtain the previous memory area
    auto previousArea = alloc_mem;
    --previousArea;
    if (previousArea->is_freespace) {
        if (previousArea->size >= alloc_mem->size) {
            memcpy(previousArea->pointer, alloc_mem->pointer, alloc_mem->size);
        } else {
            // Avoid destination and source areas crossing
            size_t remainingBytes = alloc_mem->size;
            size_t offset = 0;
            while (remainingBytes > 0){
                // Determine bytes to copy
                size_t bytesToCopy = previousArea->size;
                if (bytesToCopy > remainingBytes) {
                    bytesToCopy = remainingBytes;
                }
                // Copy memory content
                memcpy(
                    previousArea->pointer + offset,
                    alloc_mem->pointer + offset,
                    bytesToCopy
                );
                offset += bytesToCopy;
                remainingBytes -= bytesToCopy;
            }
        }
        // Swap allocated and free memory
        MemoryInfo newFreeArea(
            previousArea->pointer + alloc_mem->size, previousArea->size, true
        );
        alloc_mem->pointer = previousArea->pointer;
        this->memory_table.erase(previousArea);
        auto nextArea = alloc_mem;
        ++nextArea;
        this->memory_table.insert(nextArea, newFreeArea);
        return true;
    } else {
        return false;
    }
}

bool Allocator::AllocatorImpl::extendToNextFree(
    std::list<MemoryInfo>::iterator alloc_mem,
    size_t new_size
)
{
    // Simple checks
    if (alloc_mem == memory_table.end()) {
        return false;
    } else if (alloc_mem->is_freespace) {
        return false;
    }

    auto nextArea = alloc_mem;
    ++nextArea;
    if (nextArea == this->memory_table.end()) {
        return false;
    } else if (!nextArea->is_freespace) {
        return false;
    // Check if there is enough size to extend
    } else if (nextArea->size < new_size - alloc_mem->size) {
        return false;
    }

    // Extend the area
    nextArea->pointer += new_size - alloc_mem->size;
    nextArea->size -= new_size - alloc_mem->size;
    alloc_mem->size = new_size;
    if (nextArea->size == 0) {
        this->memory_table.erase(nextArea);
    }
    return true;
}

std::list<MemoryInfo>::iterator Allocator::AllocatorImpl::UniteFreeSpace(
    std::list<MemoryInfo>::iterator fs
)
{
    std::list<MemoryInfo>::iterator highestSpace, lowestSpace, currentSpace;
    size_t cumulativeSize = 0;

    if (fs == this->memory_table.end()) {
        return std::list<MemoryInfo>::iterator();
    }

    if (!fs->is_freespace) {
        return std::list<MemoryInfo>::iterator();
    }

    // Find the highest (with least address) free memory area of the set
    highestSpace = fs;
    while (highestSpace != this->memory_table.begin()) {
        currentSpace = highestSpace;
        --currentSpace;
        if (currentSpace->is_freespace) {
            --highestSpace;
        } else {
            break;
        }
    }

    /*
     * Find the highest (with least address) allocated memory area after the
     * set. May be end().
     */
    lowestSpace = fs;
    do {
        ++lowestSpace;
        if (lowestSpace == this->memory_table.end()) {
            break;
        }
    } while (lowestSpace->is_freespace);

    // Obtain last free memory area in the set
    currentSpace = lowestSpace;
    --currentSpace;

    // Check if there is only one free memory area in the set
    if (highestSpace != currentSpace) {
        // Calculate the memory amount
        cumulativeSize = rangeMemoryAmount(highestSpace, lowestSpace);

        // Use the first memory area of the set
        highestSpace->size = cumulativeSize;
        // Remove other memory areas
        currentSpace = highestSpace;
        ++currentSpace;
        this->memory_table.erase(currentSpace, lowestSpace);
    }

    return highestSpace;
}

/** Pre-defined. Allocator constructor. */
Allocator::Allocator(void *base, size_t size):
    impl(new AllocatorImpl)
{
    // Consider all the specified memory as free
    this->impl->memory_table.push_back(MemoryInfo(base, size, true));
}

/** Allocator destructor. Deallocates Allocator implementation strucuture. */
Allocator::~Allocator()
{
    this->impl->memory_table.clear();
    delete this->impl;
}

/** Pre-defined. Allocates the memory area with the specified size. */
Pointer Allocator::alloc(size_t N)
{

    // Invalid pointer by default
    Pointer returnPointer;

    // Invalid zero-size memory allocation.
    if (N == 0) {
        return returnPointer;
    }

    // Find the free space with the best proper size
    bool bestIsSet = false;
    size_t minAmountDifference = 0;
    std::list<MemoryInfo>::iterator bestFreeSpace;

    for (
        auto currentSpace = this->impl->memory_table.begin();
        currentSpace != this->impl->memory_table.end();
        ++currentSpace
    ) {
        if ((currentSpace->is_freespace) && (currentSpace->size >= N)) {
            if (
                (!bestIsSet) ||
                (minAmountDifference > currentSpace->size - N)
            ) {
                minAmountDifference = currentSpace->size - N;
                bestFreeSpace = currentSpace;
                bestIsSet = true;
            }
        }
    }

    if (!bestIsSet) {
        // Free space is not found
        AllocError error(AllocErrorType::NoMemory, "");
        throw error;
    }

    // Try to change free space to allocated space
    if (bestFreeSpace->size == N) {
        bestFreeSpace->is_freespace = false;
        returnPointer.impl->p = bestFreeSpace;
    } else {
        // Allocate part of free space
        MemoryInfo allocatedInfo(bestFreeSpace->pointer, N, false);
        returnPointer.impl->p = this->impl->memory_table.insert(
            bestFreeSpace, allocatedInfo
        );
        // Reduce the free space
        bestFreeSpace->pointer += N;
        bestFreeSpace->size -= N;
    }


    // Make the pointer valid
    returnPointer.impl->allocator = this;

    return returnPointer;
}

void Allocator::realloc(Pointer &p, size_t N)
{
    // Check whenever pointer is valid
    if ((p.impl->allocator) && (p.impl->allocator != this)) {
        throw AllocError(
            AllocErrorType::InvalidFree,
            "The pointer was allocated by the different allicator"
        );
    }

    // Check whenever size is valid
    if (N == 0) {
        if (p.get()) {
            this->free(p);
        }
        return;
    }

    // Just allocate for null pointer
    if (!p.get()) {
        p = this->alloc(N);
        return;
    }

    // Try the quick realloc
    if (N < p.impl->p->size) {
        // Insert a free space area after the specified one
        MemoryInfo freeSpaceInfo(
            p.impl->p->pointer + N, p.impl->p->size - N, true
        );
        auto currentSpace = p.impl->p;
        ++currentSpace;
        this->impl->memory_table.insert(currentSpace, freeSpaceInfo);
        // Change specified area size
        p.impl->p->size = N;
        return;
    } else if (N == p.impl->p->size) {
        // Do nothing
        return;
    }

    // Inplace reallocation
    if (this->impl->extendToNextFree(p.impl->p, N)) {
        return;
    }

    // Try to unite with the previous
    if (p.impl->p != this->impl->memory_table.begin()) {
        auto previousArea = p.impl->p;
        --previousArea;
        do {
            if (previousArea->size + p.impl->p->size < N) {
                break;
            }
            if (!this->impl->moveToPreviousFree(p.impl->p)) {
                break;
            }
            this->impl->extendToNextFree(p.impl->p, N);
            return;
        } while (false);
    }

    // All the simple reallocations failed
    Pointer new_p = this->alloc(N);
    memcpy(new_p.get(), p.get(), p.impl->p->size);
    this->free(p);

    p = new_p;
}
void Allocator::free(Pointer &p)
{
    // Simple checks
    if (p.impl->allocator != this) {
        throw AllocError(
            AllocErrorType::InvalidFree,
            "Unable to free the pointer. It is invalid or created by the"
            " different allocator."
        );
    }

    // Frees the memory area
    p.impl->p->is_freespace = true;
    this->impl->UniteFreeSpace(p.impl->p);

    // Make the pointer invalid
    p.impl->p = std::list<MemoryInfo>::iterator();
    p.impl->allocator = nullptr;
}

void Allocator::defrag()
{
    /* Unite free space at the beginning (begin + 1 will be the end or
     * allocated)
     */
    this->impl->UniteFreeSpace(this->impl->memory_table.begin());

    // Bubble
    for (
        auto freeElement = this->impl->memory_table.begin();
        freeElement != this->impl->memory_table.end(); ++freeElement
    ) {
        if (freeElement->is_freespace) {
            // Unite free areas
            this->impl->UniteFreeSpace(freeElement);
            // Next element will be end or allocated
            std::list<MemoryInfo>::iterator allocatedElement = freeElement;
            ++allocatedElement;

            /* Move allocated area content to the free area content.
             * Swap areas.
             */
            if (this->impl->moveToPreviousFree(allocatedElement)) {
                freeElement = allocatedElement;
            }
        }
    }
}

std::string Allocator::dump()
{
    return std::string("");
}
