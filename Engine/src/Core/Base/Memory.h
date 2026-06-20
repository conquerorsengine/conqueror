#pragma once

#include "Base.h"
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>

namespace Conqueror::Memory
{
    // Memory allocation tracking
    struct AllocationInfo
    {
        void* Address;
        size_t Size;
        const char* File;
        int Line;
        const char* Category;
        uint64_t Timestamp;
    };

    // Memory allocator interface
    class CQ_API IAllocator
    {
    public:
        virtual ~IAllocator() = default;
        
        virtual void* Allocate(size_t size, size_t alignment = 16) = 0;
        virtual void Deallocate(void* ptr) = 0;
        virtual void Reset() = 0;
        
        virtual size_t GetUsedMemory() const = 0;
        virtual size_t GetTotalMemory() const = 0;
        virtual const char* GetName() const = 0;
    };

    // Linear allocator (stack-like, fast, no individual deallocation)
    class CQ_API LinearAllocator : public IAllocator
    {
    public:
        LinearAllocator(size_t size, const char* name = "LinearAllocator");
        ~LinearAllocator();
        
        void* Allocate(size_t size, size_t alignment = 16) override;
        void Deallocate(void* ptr) override; // No-op
        void Reset() override;
        
        size_t GetUsedMemory() const override { return m_Offset; }
        size_t GetTotalMemory() const override { return m_Size; }
        const char* GetName() const override { return m_Name; }
        
    private:
        void* m_Memory;
        size_t m_Size;
        size_t m_Offset;
        const char* m_Name;
    };

    // Pool allocator (fixed-size blocks, O(1) alloc/free)
    class CQ_API PoolAllocator : public IAllocator
    {
    public:
        PoolAllocator(size_t blockSize, size_t blockCount, const char* name = "PoolAllocator");
        ~PoolAllocator();
        
        void* Allocate(size_t size, size_t alignment = 16) override;
        void Deallocate(void* ptr) override;
        void Reset() override;
        
        size_t GetUsedMemory() const override;
        size_t GetTotalMemory() const override { return m_BlockSize * m_BlockCount; }
        const char* GetName() const override { return m_Name; }
        
        size_t GetBlockSize() const { return m_BlockSize; }
        size_t GetFreeBlocks() const;
        
    private:
        void* m_Memory;
        void* m_FreeList;
        size_t m_BlockSize;
        size_t m_BlockCount;
        size_t m_UsedBlocks;
        const char* m_Name;
    };

    // Stack allocator (LIFO, fast, requires ordered deallocation)
    class CQ_API StackAllocator : public IAllocator
    {
    public:
        StackAllocator(size_t size, const char* name = "StackAllocator");
        ~StackAllocator();
        
        void* Allocate(size_t size, size_t alignment = 16) override;
        void Deallocate(void* ptr) override;
        void Reset() override;
        
        size_t GetUsedMemory() const override { return m_Offset; }
        size_t GetTotalMemory() const override { return m_Size; }
        const char* GetName() const override { return m_Name; }
        
        // Stack marker for bulk deallocation
        struct Marker
        {
            size_t Offset;
        };
        
        Marker GetMarker() const;
        void FreeToMarker(Marker marker);
        
    private:
        struct AllocationHeader
        {
            size_t Size;
            size_t PreviousOffset;
        };
        
        void* m_Memory;
        size_t m_Size;
        size_t m_Offset;
        size_t m_PreviousOffset;
        const char* m_Name;
    };

    // Double-ended stack allocator (allocate from both ends)
    class CQ_API DoubleStackAllocator : public IAllocator
    {
    public:
        DoubleStackAllocator(size_t size, const char* name = "DoubleStackAllocator");
        ~DoubleStackAllocator();
        
        void* Allocate(size_t size, size_t alignment = 16) override;
        void* AllocateFromTop(size_t size, size_t alignment = 16);
        
        void Deallocate(void* ptr) override;
        void Reset() override;
        
        size_t GetUsedMemory() const override;
        size_t GetTotalMemory() const override { return m_Size; }
        const char* GetName() const override { return m_Name; }
        
    private:
        void* m_Memory;
        size_t m_Size;
        size_t m_BottomOffset;
        size_t m_TopOffset;
        const char* m_Name;
    };

    // Memory tracker (global allocation tracking)
    class CQ_API MemoryTracker
    {
    public:
        static void Init();
        static void Shutdown();
        
        static void RecordAllocation(void* ptr, size_t size, const char* file, int line, const char* category = "General");
        static void RecordDeallocation(void* ptr);
        
        static size_t GetTotalAllocated();
        static size_t GetTotalDeallocated();
        static size_t GetCurrentUsage();
        static size_t GetPeakUsage();
        
        static size_t GetAllocationCount();
        static size_t GetDeallocationCount();
        
        // Category-based tracking
        static size_t GetCategoryUsage(const char* category);
        static void PrintMemoryReport();
        static void DumpLeaks();
        
        // Enable/disable tracking
        static void SetEnabled(bool enabled);
        static bool IsEnabled();
        
    private:
        static std::atomic<size_t> s_TotalAllocated;
        static std::atomic<size_t> s_TotalDeallocated;
        static std::atomic<size_t> s_PeakUsage;
        static std::atomic<size_t> s_AllocationCount;
        static std::atomic<size_t> s_DeallocationCount;
        static std::atomic<bool> s_Enabled;
        
        static std::mutex s_Mutex;
        static std::unordered_map<void*, AllocationInfo> s_Allocations;
        static std::unordered_map<std::string, size_t> s_CategoryUsage;
    };

    // Alignment utilities
    inline size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    inline void* AlignPointer(void* ptr, size_t alignment)
    {
        return reinterpret_cast<void*>(AlignUp(reinterpret_cast<size_t>(ptr), alignment));
    }

    inline size_t GetAlignmentOffset(void* ptr, size_t alignment)
    {
        size_t address = reinterpret_cast<size_t>(ptr);
        size_t aligned = AlignUp(address, alignment);
        return aligned - address;
    }

    // Memory utilities
    CQ_API void MemSet(void* dest, int value, size_t size);
    CQ_API void MemCopy(void* dest, const void* src, size_t size);
    CQ_API void MemMove(void* dest, const void* src, size_t size);
    CQ_API int MemCompare(const void* ptr1, const void* ptr2, size_t size);
    CQ_API void MemZero(void* dest, size_t size);
}

// Tracked allocation macros
#ifdef CQ_TRACK_MEMORY
    #define CQ_NEW(type, ...) new (Conqueror::Memory::MemoryTracker::RecordAllocation(malloc(sizeof(type)), sizeof(type), __FILE__, __LINE__, #type)) type(__VA_ARGS__)
    #define CQ_DELETE(ptr) do { Conqueror::Memory::MemoryTracker::RecordDeallocation(ptr); delete ptr; } while(0)
    #define CQ_MALLOC(size, category) Conqueror::Memory::MemoryTracker::RecordAllocation(malloc(size), size, __FILE__, __LINE__, category)
    #define CQ_FREE(ptr) do { Conqueror::Memory::MemoryTracker::RecordDeallocation(ptr); free(ptr); } while(0)
#else
    #define CQ_NEW(type, ...) new type(__VA_ARGS__)
    #define CQ_DELETE(ptr) delete ptr
    #define CQ_MALLOC(size, category) malloc(size)
    #define CQ_FREE(ptr) free(ptr)
#endif
