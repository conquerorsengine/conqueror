#pragma once

#include "Base.h"
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdlib>

namespace Conqueror::ListUtils
{
    template<typename T>
    bool Contains(const std::vector<T>& list, const T& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    template<typename T>
    int64_t IndexOf(const std::vector<T>& list, const T& item)
    {
        auto it = std::find(list.begin(), list.end(), item);
        if (it != list.end()) return static_cast<int64_t>(std::distance(list.begin(), it));
        return -1;
    }

    template<typename T>
    bool RemoveAt(std::vector<T>& list, size_t index)
    {
        if (index < list.size()) {
            list.erase(list.begin() + index);
            return true;
        }
        return false;
    }

    template<typename T>
    bool Insert(std::vector<T>& list, size_t index, const T& item)
    {
        if (index <= list.size()) {
            list.insert(list.begin() + index, item);
            return true;
        }
        return false;
    }

    template<typename T>
    void Reverse(std::vector<T>& list)
    {
        std::reverse(list.begin(), list.end());
    }

    template<typename T>
    void Shuffle(std::vector<T>& list)
    {
        if (list.empty()) return;
        for (size_t i = list.size() - 1; i > 0; --i) {
            size_t j = std::rand() % (i + 1);
            std::swap(list[i], list[j]);
        }
    }

    template<typename T>
    bool Swap(std::vector<T>& list, size_t index1, size_t index2)
    {
        if (index1 < list.size() && index2 < list.size()) {
            std::swap(list[index1], list[index2]);
            return true;
        }
        return false;
    }

    template<typename T>
    int64_t Count(const std::vector<T>& list, const T& item)
    {
        return std::count(list.begin(), list.end(), item);
    }
    
    // List Extras
    template<typename T>
    std::vector<std::vector<T>> Chunk(const std::vector<T>& list, size_t chunkSize)
    {
        std::vector<std::vector<T>> result;
        if (chunkSize == 0) return result;
        for (size_t i = 0; i < list.size(); i += chunkSize) {
            std::vector<T> chunk;
            for (size_t j = i; j < i + chunkSize && j < list.size(); ++j) chunk.push_back(list[j]);
            result.push_back(chunk);
        }
        return result;
    }

    template<typename T, typename Predicate>
    std::vector<T> Difference(const std::vector<T>& a, const std::vector<T>& b, Predicate eq)
    {
        std::vector<T> result;
        for (const auto& ea : a) {
            bool found = false;
            for (const auto& eb : b) { if (eq(ea, eb)) { found = true; break; } }
            if (!found) result.push_back(ea);
        }
        return result;
    }

    template<typename T, typename Predicate>
    std::vector<T> Intersection(const std::vector<T>& a, const std::vector<T>& b, Predicate eq)
    {
        std::vector<T> result;
        for (const auto& ea : a) {
            bool found = false;
            for (const auto& eb : b) { if (eq(ea, eb)) { found = true; break; } }
            if (found) result.push_back(ea);
        }
        return result;
    }

    template<typename T, typename Predicate>
    std::vector<T> Union(const std::vector<T>& a, const std::vector<T>& b, Predicate eq)
    {
        std::vector<T> result;
        auto addUnique = [&](const std::vector<T>& src) {
            for (const auto& e : src) {
                bool found = false;
                for (const auto& re : result) { if (eq(e, re)) { found = true; break; } }
                if (!found) result.push_back(e);
            }
        };
        addUnique(a); addUnique(b);
        return result;
    }
    
    template<typename T, typename Predicate>
    std::vector<T> Without(const std::vector<T>& list, const std::vector<T>& excludes, Predicate eq)
    {
        std::vector<T> result;
        for (const auto& e : list) {
            bool remove = false;
            for (const auto& ex : excludes) {
                if (eq(e, ex)) { remove = true; break; }
            }
            if (!remove) result.push_back(e);
        }
        return result;
    }
    
    template<typename T>
    void Pad(std::vector<T>& list, size_t size, const T& padValue)
    {
        while (list.size() < size) list.push_back(padValue);
    }
    
    template<typename T, typename Predicate>
    int64_t LastIndexOf(const std::vector<T>& list, const T& target, Predicate eq)
    {
        for (int i = (int)list.size() - 1; i >= 0; --i) {
            if (eq(list[i], target)) return i;
        }
        return -1;
    }
    
    template<typename T>
    std::vector<T> Drop(const std::vector<T>& list, size_t n)
    {
        std::vector<T> result;
        for (size_t i = n; i < list.size(); ++i) result.push_back(list[i]);
        return result;
    }
    
    template<typename T>
    std::vector<T> Take(const std::vector<T>& list, size_t n)
    {
        std::vector<T> result;
        for (size_t i = 0; i < n && i < list.size(); ++i) result.push_back(list[i]);
        return result;
    }
    
    template<typename T>
    void Rotate(std::vector<T>& list, int n)
    {
        if (list.empty()) return;
        n = n % (int)list.size();
        if (n < 0) n += (int)list.size();
        std::rotate(list.rbegin(), list.rbegin() + n, list.rend());
    }
    
    template<typename T, typename U>
    std::vector<std::pair<T, U>> Zip(const std::vector<T>& list1, const std::vector<U>& list2)
    {
        std::vector<std::pair<T, U>> result;
        size_t len = std::min(list1.size(), list2.size());
        for (size_t i = 0; i < len; ++i) {
            result.push_back({list1[i], list2[i]});
        }
        return result;
    }

    template<typename T, typename Op>
    T Product(const std::vector<T>& list, T initValue, Op op)
    {
        T prod = initValue;
        for (const auto& v : list) prod = op(prod, v);
        return prod;
    }

    template<typename T, typename Compare>
    void Sort(std::vector<T>& list, Compare cmp) {
        std::sort(list.begin(), list.end(), cmp);
    }

    template<typename T>
    std::vector<T> Slice(const std::vector<T>& list, int start, int end) {
        std::vector<T> result;
        if (start < 0) start = 0;
        if (end > (int)list.size()) end = (int)list.size();
        for (int i = start; i < end; ++i) result.push_back(list[i]);
        return result;
    }

    template<typename T>
    std::vector<T> Flatten(const std::vector<std::vector<T>>& list) {
        std::vector<T> result;
        for (const auto& inner : list) {
            result.insert(result.end(), inner.begin(), inner.end());
        }
        return result;
    }

    template<typename T, typename KeyFunc>
    std::vector<T> Unique(const std::vector<T>& list, KeyFunc keyFunc) {
        std::vector<T> result;
        std::vector<std::string> seen;
        for (const auto& elem : list) {
            std::string key = keyFunc(elem);
            bool found = false;
            for (const auto& s : seen) if (s == key) { found = true; break; }
            if (!found) { seen.push_back(key); result.push_back(elem); }
        }
        return result;
    }

    template<typename T>
    std::vector<T> Fill(const T& val, int count) {
        std::vector<T> result;
        for (int i = 0; i < count; ++i) result.push_back(val);
        return result;
    }

    template<typename T>
    std::vector<T> Repeat(const std::vector<T>& list, int count) {
        std::vector<T> result;
        for (int i = 0; i < count; ++i) {
            result.insert(result.end(), list.begin(), list.end());
        }
        return result;
    }

    template<typename T>
    bool Pop(std::vector<T>& list, T& outVal) {
        if (list.empty()) return false;
        outVal = list.back();
        list.pop_back();
        return true;
    }

    template<typename T, typename NumType = double, typename Extractor>
    bool MinNumeric(const std::vector<T>& list, Extractor ext, NumType& outMin) {
        bool found = false;
        for (const auto& v : list) {
            NumType val;
            if (ext(v, val)) {
                if (!found || val < outMin) { outMin = val; found = true; }
            }
        }
        return found;
    }

    template<typename T, typename NumType = double, typename Extractor>
    bool MaxNumeric(const std::vector<T>& list, Extractor ext, NumType& outMax) {
        bool found = false;
        for (const auto& v : list) {
            NumType val;
            if (ext(v, val)) {
                if (!found || val > outMax) { outMax = val; found = true; }
            }
        }
        return found;
    }

    template<typename T, typename NumType = double, typename Extractor>
    NumType SumNumeric(const std::vector<T>& list, Extractor ext) {
        NumType sum = 0;
        for (const auto& v : list) {
            NumType val;
            if (ext(v, val)) sum += val;
        }
        return sum;
    }

    template<typename T, typename NumType = double, typename Extractor>
    bool AverageNumeric(const std::vector<T>& list, Extractor ext, NumType& outAvg) {
        NumType sum = 0;
        int count = 0;
        for (const auto& v : list) {
            NumType val;
            if (ext(v, val)) {
                sum += val;
                count++;
            }
        }
        if (count == 0) return false;
        outAvg = sum / count;
        return true;
    }

    template<typename T, typename ToStringFunc>
    std::string Join(const std::vector<T>& list, const std::string& separator, ToStringFunc toStr) {
        std::string result = "";
        for (size_t i = 0; i < list.size(); ++i) {
            result += toStr(list[i]);
            if (i < list.size() - 1) result += separator;
        }
        return result;
    }
}
