#pragma once

#include "Base.h"
#include <cstddef>
#include <cstring>
#include <utility>
#include <iterator>

namespace Conqueror
{
    // Ring buffer (circular buffer, fixed size)
    template<typename T, size_t Capacity>
    class RingBuffer
    {
    public:
        RingBuffer() : m_Head(0), m_Tail(0), m_Size(0) {}
        
        bool Push(const T& value)
        {
            if (IsFull()) return false;
            
            m_Data[m_Tail] = value;
            m_Tail = (m_Tail + 1) % Capacity;
            m_Size++;
            return true;
        }
        
        bool Push(T&& value)
        {
            if (IsFull()) return false;
            
            m_Data[m_Tail] = std::move(value);
            m_Tail = (m_Tail + 1) % Capacity;
            m_Size++;
            return true;
        }
        
        bool Pop(T& value)
        {
            if (IsEmpty()) return false;
            
            value = std::move(m_Data[m_Head]);
            m_Head = (m_Head + 1) % Capacity;
            m_Size--;
            return true;
        }
        
        const T& Front() const { return m_Data[m_Head]; }
        T& Front() { return m_Data[m_Head]; }
        
        const T& Back() const { return m_Data[(m_Tail + Capacity - 1) % Capacity]; }
        T& Back() { return m_Data[(m_Tail + Capacity - 1) % Capacity]; }
        
        bool IsEmpty() const { return m_Size == 0; }
        bool IsFull() const { return m_Size == Capacity; }
        size_t Size() const { return m_Size; }
        size_t GetCapacity() const { return Capacity; }
        
        void Clear() { m_Head = m_Tail = m_Size = 0; }
        
    private:
        T m_Data[Capacity];
        size_t m_Head;
        size_t m_Tail;
        size_t m_Size;
    };

    // Sparse set (fast iteration, O(1) insert/remove/contains)
    template<typename T>
    class SparseSet
    {
    public:
        SparseSet(size_t maxSize = 1024) : m_MaxSize(maxSize)
        {
            m_Sparse.resize(maxSize, INVALID_INDEX);
        }
        
        void Insert(size_t index, const T& value)
        {
            if (index >= m_MaxSize || Contains(index)) return;
            
            m_Sparse[index] = m_Dense.size();
            m_Dense.push_back(index);
            m_Values.push_back(value);
        }
        
        void Remove(size_t index)
        {
            if (!Contains(index)) return;
            
            size_t denseIndex = m_Sparse[index];
            size_t lastIndex = m_Dense.back();
            
            // Swap with last element
            m_Dense[denseIndex] = lastIndex;
            m_Values[denseIndex] = m_Values.back();
            m_Sparse[lastIndex] = denseIndex;
            
            m_Dense.pop_back();
            m_Values.pop_back();
            m_Sparse[index] = INVALID_INDEX;
        }
        
        bool Contains(size_t index) const
        {
            return index < m_MaxSize && m_Sparse[index] != INVALID_INDEX;
        }
        
        T& Get(size_t index)
        {
            return m_Values[m_Sparse[index]];
        }
        
        const T& Get(size_t index) const
        {
            return m_Values[m_Sparse[index]];
        }
        
        size_t Size() const { return m_Dense.size(); }
        bool IsEmpty() const { return m_Dense.empty(); }
        void Clear()
        {
            m_Dense.clear();
            m_Values.clear();
            std::fill(m_Sparse.begin(), m_Sparse.end(), INVALID_INDEX);
        }
        
        // Iteration
        auto begin() { return m_Values.begin(); }
        auto end() { return m_Values.end(); }
        auto begin() const { return m_Values.begin(); }
        auto end() const { return m_Values.end(); }
        
    private:
        static constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);
        
        std::vector<size_t> m_Sparse;  // Index -> Dense index
        std::vector<size_t> m_Dense;   // Dense index -> Index
        std::vector<T> m_Values;       // Dense index -> Value
        size_t m_MaxSize;
    };

    // Free list (for object pooling)
    template<typename T>
    class FreeList
    {
    public:
        FreeList(size_t initialCapacity = 64)
        {
            m_Data.reserve(initialCapacity);
        }
        
        size_t Allocate(const T& value)
        {
            if (m_FreeIndices.empty())
            {
                size_t index = m_Data.size();
                m_Data.push_back(value);
                return index;
            }
            
            size_t index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            m_Data[index] = value;
            return index;
        }
        
        void Free(size_t index)
        {
            if (index >= m_Data.size()) return;
            m_FreeIndices.push_back(index);
        }
        
        T& Get(size_t index) { return m_Data[index]; }
        const T& Get(size_t index) const { return m_Data[index]; }
        
        bool IsValid(size_t index) const
        {
            if (index >= m_Data.size()) return false;
            return std::find(m_FreeIndices.begin(), m_FreeIndices.end(), index) == m_FreeIndices.end();
        }
        
        size_t Size() const { return m_Data.size() - m_FreeIndices.size(); }
        size_t Capacity() const { return m_Data.size(); }
        
        void Clear()
        {
            m_Data.clear();
            m_FreeIndices.clear();
        }
        
    private:
        std::vector<T> m_Data;
        std::vector<size_t> m_FreeIndices;
    };

    // Handle-based container (stable handles, internal reordering allowed)
    template<typename T>
    class HandleContainer
    {
    public:
        struct Handle
        {
            uint32_t Index;
            uint32_t Generation;
            
            bool operator==(const Handle& other) const
            {
                return Index == other.Index && Generation == other.Generation;
            }
            
            bool operator!=(const Handle& other) const
            {
                return !(*this == other);
            }
        };
        
        HandleContainer() = default;
        
        Handle Add(const T& value)
        {
            if (m_FreeIndices.empty())
            {
                uint32_t index = static_cast<uint32_t>(m_Data.size());
                m_Data.push_back(value);
                m_Generations.push_back(0);
                return Handle{ index, 0 };
            }
            
            uint32_t index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            m_Data[index] = value;
            return Handle{ index, m_Generations[index] };
        }
        
        void Remove(Handle handle)
        {
            if (!IsValid(handle)) return;
            
            m_Generations[handle.Index]++;
            m_FreeIndices.push_back(handle.Index);
        }
        
        bool IsValid(Handle handle) const
        {
            return handle.Index < m_Data.size() && 
                   m_Generations[handle.Index] == handle.Generation;
        }
        
        T& Get(Handle handle) { return m_Data[handle.Index]; }
        const T& Get(Handle handle) const { return m_Data[handle.Index]; }
        
        T* TryGet(Handle handle)
        {
            return IsValid(handle) ? &m_Data[handle.Index] : nullptr;
        }
        
        const T* TryGet(Handle handle) const
        {
            return IsValid(handle) ? &m_Data[handle.Index] : nullptr;
        }
        
        size_t Size() const { return m_Data.size() - m_FreeIndices.size(); }
        void Clear()
        {
            m_Data.clear();
            m_Generations.clear();
            m_FreeIndices.clear();
        }
        
    private:
        std::vector<T> m_Data;
        std::vector<uint32_t> m_Generations;
        std::vector<uint32_t> m_FreeIndices;
    };

    // Slot map (stable handles + cache-friendly iteration)
    template<typename T>
    class SlotMap
    {
    public:
        struct Key
        {
            uint32_t Index;
            uint32_t Generation;
        };
        
        SlotMap() = default;
        
        Key Insert(const T& value)
        {
            if (m_FreeList.empty())
            {
                uint32_t index = static_cast<uint32_t>(m_Slots.size());
                m_Slots.push_back({ static_cast<uint32_t>(m_Data.size()), 0 });
                m_Data.push_back(value);
                m_Erase.push_back(index);
                return Key{ index, 0 };
            }
            
            uint32_t index = m_FreeList.back();
            m_FreeList.pop_back();
            
            Slot& slot = m_Slots[index];
            slot.Index = static_cast<uint32_t>(m_Data.size());
            
            m_Data.push_back(value);
            m_Erase.push_back(index);
            
            return Key{ index, slot.Generation };
        }
        
        void Remove(Key key)
        {
            if (!IsValid(key)) return;
            
            Slot& slot = m_Slots[key.Index];
            uint32_t dataIndex = slot.Index;
            
            // Swap with last
            if (dataIndex != m_Data.size() - 1)
            {
                m_Data[dataIndex] = std::move(m_Data.back());
                uint32_t movedSlotIndex = m_Erase.back();
                m_Slots[movedSlotIndex].Index = dataIndex;
                m_Erase[dataIndex] = movedSlotIndex;
            }
            
            m_Data.pop_back();
            m_Erase.pop_back();
            
            slot.Generation++;
            m_FreeList.push_back(key.Index);
        }
        
        bool IsValid(Key key) const
        {
            return key.Index < m_Slots.size() && 
                   m_Slots[key.Index].Generation == key.Generation;
        }
        
        T& Get(Key key) { return m_Data[m_Slots[key.Index].Index]; }
        const T& Get(Key key) const { return m_Data[m_Slots[key.Index].Index]; }
        
        size_t Size() const { return m_Data.size(); }
        
        auto begin() { return m_Data.begin(); }
        auto end() { return m_Data.end(); }
        auto begin() const { return m_Data.begin(); }
        auto end() const { return m_Data.end(); }
        
    private:
        struct Slot
        {
            uint32_t Index;
            uint32_t Generation;
        };
        
        std::vector<Slot> m_Slots;
        std::vector<T> m_Data;
        std::vector<uint32_t> m_Erase;  // Data index -> Slot index
        std::vector<uint32_t> m_FreeList;
    };
}
