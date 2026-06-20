#include "Memory.h"
#include "Core/Logging/Log.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace Conqueror::Memory
{
    // MemoryTracker static members
    std::atomic<size_t> MemoryTracker::s_TotalAllocated{0};
    std::atomic<size_t> MemoryTracker::s_TotalDeallocated{0};
    std::atomic<size_t> MemoryTracker::s_PeakUsage{0};
    std::atomic<size_t> MemoryTracker::s_AllocationCount{0};
    std::atomic<size_t> MemoryTracker::s_DeallocationCount{0};
    std::atomic<bool> MemoryTracker::s_Enabled{false};
    std::mutex MemoryTracker::s_Mutex;
    std::unordered_map<void*, AllocationInfo> MemoryTracker::s_Allocations;
    std::unordered_map<std::string, size_t> MemoryTracker::s_CategoryUsage;

    // LinearAllocator
    LinearAllocator::LinearAllocator(size_t size, const char* name)
        : m_Size(size), m_Offset(0), m_Name(name)
    {
        m_Memory = std::malloc(size);
        CQ_CORE_ASSERT(m_Memory, "LinearAllocator: Failed to allocate memory");
    }

    LinearAllocator::~LinearAllocator()
    {
        std::free(m_Memory);
    }

    void* LinearAllocator::Allocate(size_t size, size_t alignment)
    {
        size_t alignmentOffset = GetAlignmentOffset(static_cast<char*>(m_Memory) + m_Offset, alignment);
        size_t totalSize = size + alignmentOffset;
        
        if (m_Offset + totalSize > m_Size)
        {
            CQ_CORE_ERROR("LinearAllocator: Out of memory (requested: {0}, available: {1})", totalSize, m_Size - m_Offset);
            return nullptr;
        }
        
        void* ptr = static_cast<char*>(m_Memory) + m_Offset + alignmentOffset;
        m_Offset += totalSize;
        
        return ptr;
    }

    void LinearAllocator::Deallocate(void* ptr)
    {
        // Linear allocator doesn't support individual deallocation
    }

    void LinearAllocator::Reset()
    {
        m_Offset = 0;
    }

    // PoolAllocator
    PoolAllocator::PoolAllocator(size_t blockSize, size_t blockCount, const char* name)
        : m_BlockSize(AlignUp(blockSize, 16)), m_BlockCount(blockCount), m_UsedBlocks(0), m_Name(name)
    {
        size_t totalSize = m_BlockSize * m_BlockCount;
        m_Memory = std::malloc(totalSize);
        CQ_CORE_ASSERT(m_Memory, "PoolAllocator: Failed to allocate memory");
        
        // Initialize free list
        m_FreeList = m_Memory;
        void** current = static_cast<void**>(m_FreeList);
        
        for (size_t i = 0; i < m_BlockCount - 1; ++i)
        {
            void* next = static_cast<char*>(m_Memory) + (i + 1) * m_BlockSize;
            *current = next;
            current = static_cast<void**>(next);
        }
        
        *current = nullptr; // Last block points to null
    }

    PoolAllocator::~PoolAllocator()
    {
        std::free(m_Memory);
    }

    void* PoolAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size > m_BlockSize)
        {
            CQ_CORE_ERROR("PoolAllocator: Requested size ({0}) exceeds block size ({1})", size, m_BlockSize);
            return nullptr;
        }
        
        if (m_FreeList == nullptr)
        {
            CQ_CORE_ERROR("PoolAllocator: Out of memory (all blocks used)");
            return nullptr;
        }
        
        void* ptr = m_FreeList;
        m_FreeList = *static_cast<void**>(m_FreeList);
        m_UsedBlocks++;
        
        return ptr;
    }

    void PoolAllocator::Deallocate(void* ptr)
    {
        if (ptr == nullptr)
            return;
        
        // Add block back to free list
        *static_cast<void**>(ptr) = m_FreeList;
        m_FreeList = ptr;
        m_UsedBlocks--;
    }

    void PoolAllocator::Reset()
    {
        // Rebuild free list
        m_FreeList = m_Memory;
        void** current = static_cast<void**>(m_FreeList);
        
        for (size_t i = 0; i < m_BlockCount - 1; ++i)
        {
            void* next = static_cast<char*>(m_Memory) + (i + 1) * m_BlockSize;
            *current = next;
            current = static_cast<void**>(next);
        }
        
        *current = nullptr;
        m_UsedBlocks = 0;
    }

    size_t PoolAllocator::GetUsedMemory() const
    {
        return m_UsedBlocks * m_BlockSize;
    }

    size_t PoolAllocator::GetFreeBlocks() const
    {
        return m_BlockCount - m_UsedBlocks;
    }

    // StackAllocator
    StackAllocator::StackAllocator(size_t size, const char* name)
        : m_Size(size), m_Offset(0), m_PreviousOffset(0), m_Name(name)
    {
        m_Memory = std::malloc(size);
        CQ_CORE_ASSERT(m_Memory, "StackAllocator: Failed to allocate memory");
    }

    StackAllocator::~StackAllocator()
    {
        std::free(m_Memory);
    }

    void* StackAllocator::Allocate(size_t size, size_t alignment)
    {
        size_t headerSize = sizeof(AllocationHeader);
        size_t alignmentOffset = GetAlignmentOffset(static_cast<char*>(m_Memory) + m_Offset + headerSize, alignment);
        size_t totalSize = headerSize + alignmentOffset + size;
        
        if (m_Offset + totalSize > m_Size)
        {
            CQ_CORE_ERROR("StackAllocator: Out of memory");
            return nullptr;
        }
        
        // Store header
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(static_cast<char*>(m_Memory) + m_Offset);
        header->Size = totalSize;
        header->PreviousOffset = m_PreviousOffset;
        
        void* ptr = static_cast<char*>(m_Memory) + m_Offset + headerSize + alignmentOffset;
        
        m_PreviousOffset = m_Offset;
        m_Offset += totalSize;
        
        return ptr;
    }

    void StackAllocator::Deallocate(void* ptr)
    {
        if (ptr == nullptr)
            return;
        
        // Calculate header position
        size_t ptrOffset = static_cast<char*>(ptr) - static_cast<char*>(m_Memory);
        
        // Find header (scan backwards)
        for (size_t offset = m_PreviousOffset; offset < ptrOffset; )
        {
            AllocationHeader* header = reinterpret_cast<AllocationHeader*>(static_cast<char*>(m_Memory) + offset);
            size_t nextOffset = offset + header->Size;
            
            if (nextOffset >= ptrOffset)
            {
                // Found the allocation
                m_Offset = offset;
                m_PreviousOffset = header->PreviousOffset;
                return;
            }
            
            offset = nextOffset;
        }
        
        CQ_CORE_WARN("StackAllocator: Invalid deallocation (pointer not found)");
    }

    void StackAllocator::Reset()
    {
        m_Offset = 0;
        m_PreviousOffset = 0;
    }

    StackAllocator::Marker StackAllocator::GetMarker() const
    {
        return Marker{ m_Offset };
    }

    void StackAllocator::FreeToMarker(Marker marker)
    {
        m_Offset = marker.Offset;
        
        // Recalculate previous offset
        if (m_Offset == 0)
        {
            m_PreviousOffset = 0;
        }
        else
        {
            // Scan to find previous allocation
            size_t offset = 0;
            while (offset < m_Offset)
            {
                AllocationHeader* header = reinterpret_cast<AllocationHeader*>(static_cast<char*>(m_Memory) + offset);
                size_t nextOffset = offset + header->Size;
                
                if (nextOffset >= m_Offset)
                {
                    m_PreviousOffset = offset;
                    break;
                }
                
                offset = nextOffset;
            }
        }
    }

    // DoubleStackAllocator
    DoubleStackAllocator::DoubleStackAllocator(size_t size, const char* name)
        : m_Size(size), m_BottomOffset(0), m_TopOffset(size), m_Name(name)
    {
        m_Memory = std::malloc(size);
        CQ_CORE_ASSERT(m_Memory, "DoubleStackAllocator: Failed to allocate memory");
    }

    DoubleStackAllocator::~DoubleStackAllocator()
    {
        std::free(m_Memory);
    }

    void* DoubleStackAllocator::Allocate(size_t size, size_t alignment)
    {
        size_t alignmentOffset = GetAlignmentOffset(static_cast<char*>(m_Memory) + m_BottomOffset, alignment);
        size_t totalSize = size + alignmentOffset;
        
        if (m_BottomOffset + totalSize > m_TopOffset)
        {
            CQ_CORE_ERROR("DoubleStackAllocator: Out of memory");
            return nullptr;
        }
        
        void* ptr = static_cast<char*>(m_Memory) + m_BottomOffset + alignmentOffset;
        m_BottomOffset += totalSize;
        
        return ptr;
    }

    void* DoubleStackAllocator::AllocateFromTop(size_t size, size_t alignment)
    {
        size_t alignedSize = AlignUp(size, alignment);
        
        if (m_TopOffset < m_BottomOffset + alignedSize)
        {
            CQ_CORE_ERROR("DoubleStackAllocator: Out of memory");
            return nullptr;
        }
        
        m_TopOffset -= alignedSize;
        void* ptr = static_cast<char*>(m_Memory) + m_TopOffset;
        
        return ptr;
    }

    void DoubleStackAllocator::Deallocate(void* ptr)
    {
        // Not supported for double stack
    }

    void DoubleStackAllocator::Reset()
    {
        m_BottomOffset = 0;
        m_TopOffset = m_Size;
    }

    size_t DoubleStackAllocator::GetUsedMemory() const
    {
        return m_BottomOffset + (m_Size - m_TopOffset);
    }

    // MemoryTracker
    void MemoryTracker::Init()
    {
        s_Enabled = true;
        CQ_CORE_INFO("MemoryTracker initialized");
    }

    void MemoryTracker::Shutdown()
    {
        PrintMemoryReport();
        DumpLeaks();
        s_Enabled = false;
    }

    void MemoryTracker::RecordAllocation(void* ptr, size_t size, const char* file, int line, const char* category)
    {
        if (!s_Enabled || ptr == nullptr)
            return;
        
        std::lock_guard<std::mutex> lock(s_Mutex);
        
        AllocationInfo info;
        info.Address = ptr;
        info.Size = size;
        info.File = file;
        info.Line = line;
        info.Category = category;
        info.Timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        
        s_Allocations[ptr] = info;
        s_CategoryUsage[category] += size;
        
        s_TotalAllocated += size;
        s_AllocationCount++;
        
        size_t currentUsage = s_TotalAllocated - s_TotalDeallocated;
        size_t peak = s_PeakUsage.load();
        while (currentUsage > peak && !s_PeakUsage.compare_exchange_weak(peak, currentUsage));
    }

    void MemoryTracker::RecordDeallocation(void* ptr)
    {
        if (!s_Enabled || ptr == nullptr)
            return;
        
        std::lock_guard<std::mutex> lock(s_Mutex);
        
        auto it = s_Allocations.find(ptr);
        if (it != s_Allocations.end())
        {
            s_TotalDeallocated += it->second.Size;
            s_CategoryUsage[it->second.Category] -= it->second.Size;
            s_DeallocationCount++;
            s_Allocations.erase(it);
        }
    }

    size_t MemoryTracker::GetTotalAllocated()
    {
        return s_TotalAllocated;
    }

    size_t MemoryTracker::GetTotalDeallocated()
    {
        return s_TotalDeallocated;
    }

    size_t MemoryTracker::GetCurrentUsage()
    {
        return s_TotalAllocated - s_TotalDeallocated;
    }

    size_t MemoryTracker::GetPeakUsage()
    {
        return s_PeakUsage;
    }

    size_t MemoryTracker::GetAllocationCount()
    {
        return s_AllocationCount;
    }

    size_t MemoryTracker::GetDeallocationCount()
    {
        return s_DeallocationCount;
    }

    size_t MemoryTracker::GetCategoryUsage(const char* category)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = s_CategoryUsage.find(category);
        return it != s_CategoryUsage.end() ? it->second : 0;
    }

    void MemoryTracker::PrintMemoryReport()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        
        CQ_CORE_INFO("=== Memory Report ===");
        CQ_CORE_INFO("Total Allocated: {0} bytes ({1} MB)", s_TotalAllocated.load(), s_TotalAllocated.load() / (1024 * 1024));
        CQ_CORE_INFO("Total Deallocated: {0} bytes ({1} MB)", s_TotalDeallocated.load(), s_TotalDeallocated.load() / (1024 * 1024));
        CQ_CORE_INFO("Current Usage: {0} bytes ({1} MB)", GetCurrentUsage(), GetCurrentUsage() / (1024 * 1024));
        CQ_CORE_INFO("Peak Usage: {0} bytes ({1} MB)", s_PeakUsage.load(), s_PeakUsage.load() / (1024 * 1024));
        CQ_CORE_INFO("Allocation Count: {0}", s_AllocationCount.load());
        CQ_CORE_INFO("Deallocation Count: {0}", s_DeallocationCount.load());
        CQ_CORE_INFO("Active Allocations: {0}", s_Allocations.size());
        
        CQ_CORE_INFO("=== Category Usage ===");
        for (const auto& [category, usage] : s_CategoryUsage)
        {
            if (usage > 0)
                CQ_CORE_INFO("  {0}: {1} bytes ({2} MB)", category, usage, usage / (1024 * 1024));
        }
    }

    void MemoryTracker::DumpLeaks()
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        
        if (s_Allocations.empty())
        {
            CQ_CORE_INFO("No memory leaks detected");
            return;
        }
        
        CQ_CORE_WARN("=== Memory Leaks Detected ===");
        CQ_CORE_WARN("Total leaks: {0} allocations, {1} bytes", s_Allocations.size(), GetCurrentUsage());
        
        for (const auto& [ptr, info] : s_Allocations)
        {
            CQ_CORE_WARN("  Leak: {0} bytes at {1} (allocated at {2}:{3}, category: {4})", 
                         info.Size, ptr, info.File, info.Line, info.Category);
        }
    }

    void MemoryTracker::SetEnabled(bool enabled)
    {
        s_Enabled = enabled;
    }

    bool MemoryTracker::IsEnabled()
    {
        return s_Enabled;
    }

    // Memory utilities
    void MemSet(void* dest, int value, size_t size)
    {
        std::memset(dest, value, size);
    }

    void MemCopy(void* dest, const void* src, size_t size)
    {
        std::memcpy(dest, src, size);
    }

    void MemMove(void* dest, const void* src, size_t size)
    {
        std::memmove(dest, src, size);
    }

    int MemCompare(const void* ptr1, const void* ptr2, size_t size)
    {
        return std::memcmp(ptr1, ptr2, size);
    }

    void MemZero(void* dest, size_t size)
    {
        std::memset(dest, 0, size);
    }
}
