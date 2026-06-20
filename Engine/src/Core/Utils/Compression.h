#pragma once

#include "Core/Base/Base.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Conqueror
{
    // Compression algorithms
    enum class CompressionAlgorithm
    {
        None,
        Zlib,    // zlib compression
        Zstd,    // Zstandard compression (faster)
        LZ4      // LZ4 compression (fastest)
    };

    // Compression level
    enum class CompressionLevel
    {
        Fastest = 1,
        Fast = 3,
        Default = 6,
        Best = 9
    };

    namespace Compression
    {
        // Compress data
        CQ_API bool Compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, 
                            CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd,
                            CompressionLevel level = CompressionLevel::Default);
        
        CQ_API bool Compress(const void* input, size_t inputSize, std::vector<uint8_t>& output,
                            CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd,
                            CompressionLevel level = CompressionLevel::Default);

        // Decompress data
        CQ_API bool Decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output,
                              CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd);
        
        CQ_API bool Decompress(const void* input, size_t inputSize, std::vector<uint8_t>& output,
                              CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd);

        // Compress file
        CQ_API bool CompressFile(const std::string& inputPath, const std::string& outputPath,
                                CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd,
                                CompressionLevel level = CompressionLevel::Default);

        // Decompress file
        CQ_API bool DecompressFile(const std::string& inputPath, const std::string& outputPath,
                                  CompressionAlgorithm algorithm = CompressionAlgorithm::Zstd);

        // Get compression ratio
        CQ_API float GetCompressionRatio(size_t originalSize, size_t compressedSize);

        // Get compressed size estimate
        CQ_API size_t GetMaxCompressedSize(size_t inputSize, CompressionAlgorithm algorithm);
    }
}
