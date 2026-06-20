#include "Compression.h"
#include "FileSystem.h"
#include "Core/Logging/Log.h"

#include <zlib.h>
#include <zstd.h>

namespace Conqueror
{
    namespace Compression
    {
        bool Compress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output, 
                     CompressionAlgorithm algorithm, CompressionLevel level)
        {
            return Compress(input.data(), input.size(), output, algorithm, level);
        }

        bool Compress(const void* input, size_t inputSize, std::vector<uint8_t>& output,
                     CompressionAlgorithm algorithm, CompressionLevel level)
        {
            if (!input || inputSize == 0)
            {
                CQ_CORE_ERROR("Compression: Invalid input");
                return false;
            }

            switch (algorithm)
            {
                case CompressionAlgorithm::Zlib:
                {
                    uLongf compressedSize = compressBound(inputSize);
                    output.resize(compressedSize);
                    
                    int result = compress2(output.data(), &compressedSize, 
                                         (const Bytef*)input, inputSize, 
                                         static_cast<int>(level));
                    
                    if (result != Z_OK)
                    {
                        CQ_CORE_ERROR("Compression: Zlib compression failed with code {}", result);
                        return false;
                    }
                    
                    output.resize(compressedSize);
                    return true;
                }

                case CompressionAlgorithm::Zstd:
                {
                    size_t maxCompressedSize = ZSTD_compressBound(inputSize);
                    output.resize(maxCompressedSize);
                    
                    size_t compressedSize = ZSTD_compress(output.data(), maxCompressedSize,
                                                         input, inputSize,
                                                         static_cast<int>(level));
                    
                    if (ZSTD_isError(compressedSize))
                    {
                        CQ_CORE_ERROR("Compression: Zstd compression failed: {}", 
                                     ZSTD_getErrorName(compressedSize));
                        return false;
                    }
                    
                    output.resize(compressedSize);
                    return true;
                }

                case CompressionAlgorithm::None:
                    output.assign((const uint8_t*)input, (const uint8_t*)input + inputSize);
                    return true;

                default:
                    CQ_CORE_ERROR("Compression: Unsupported algorithm");
                    return false;
            }
        }

        bool Decompress(const std::vector<uint8_t>& input, std::vector<uint8_t>& output,
                       CompressionAlgorithm algorithm)
        {
            return Decompress(input.data(), input.size(), output, algorithm);
        }

        bool Decompress(const void* input, size_t inputSize, std::vector<uint8_t>& output,
                       CompressionAlgorithm algorithm)
        {
            if (!input || inputSize == 0)
            {
                CQ_CORE_ERROR("Decompression: Invalid input");
                return false;
            }

            switch (algorithm)
            {
                case CompressionAlgorithm::Zlib:
                {
                    // Başlangıç boyutu tahmin et (genelde 2-4x)
                    uLongf decompressedSize = inputSize * 4;
                    output.resize(decompressedSize);
                    
                    int result = uncompress(output.data(), &decompressedSize,
                                          (const Bytef*)input, inputSize);
                    
                    // Eğer buffer yetersizse büyüt
                    while (result == Z_BUF_ERROR)
                    {
                        decompressedSize *= 2;
                        output.resize(decompressedSize);
                        result = uncompress(output.data(), &decompressedSize,
                                          (const Bytef*)input, inputSize);
                    }
                    
                    if (result != Z_OK)
                    {
                        CQ_CORE_ERROR("Decompression: Zlib decompression failed with code {}", result);
                        return false;
                    }
                    
                    output.resize(decompressedSize);
                    return true;
                }

                case CompressionAlgorithm::Zstd:
                {
                    unsigned long long decompressedSize = ZSTD_getFrameContentSize(input, inputSize);
                    
                    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR)
                    {
                        CQ_CORE_ERROR("Decompression: Not compressed by zstd");
                        return false;
                    }
                    
                    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN)
                    {
                        // Boyut bilinmiyor, tahmin et
                        decompressedSize = inputSize * 4;
                    }
                    
                    output.resize(decompressedSize);
                    
                    size_t result = ZSTD_decompress(output.data(), decompressedSize,
                                                   input, inputSize);
                    
                    if (ZSTD_isError(result))
                    {
                        CQ_CORE_ERROR("Decompression: Zstd decompression failed: {}", 
                                     ZSTD_getErrorName(result));
                        return false;
                    }
                    
                    output.resize(result);
                    return true;
                }

                case CompressionAlgorithm::None:
                    output.assign((const uint8_t*)input, (const uint8_t*)input + inputSize);
                    return true;

                default:
                    CQ_CORE_ERROR("Decompression: Unsupported algorithm");
                    return false;
            }
        }

        bool CompressFile(const std::string& inputPath, const std::string& outputPath,
                         CompressionAlgorithm algorithm, CompressionLevel level)
        {
            std::vector<uint8_t> inputData;
            if (!FileSystem::ReadFileBinary(inputPath, inputData))
            {
                CQ_CORE_ERROR("Compression: Failed to read input file: {}", inputPath);
                return false;
            }

            std::vector<uint8_t> compressedData;
            if (!Compress(inputData, compressedData, algorithm, level))
            {
                return false;
            }

            if (!FileSystem::WriteFileBinary(outputPath, compressedData))
            {
                CQ_CORE_ERROR("Compression: Failed to write output file: {}", outputPath);
                return false;
            }

            CQ_CORE_INFO("Compressed {} -> {} (ratio: {:.2f}%)", 
                        inputPath, outputPath, 
                        GetCompressionRatio(inputData.size(), compressedData.size()));
            
            return true;
        }

        bool DecompressFile(const std::string& inputPath, const std::string& outputPath,
                           CompressionAlgorithm algorithm)
        {
            std::vector<uint8_t> compressedData;
            if (!FileSystem::ReadFileBinary(inputPath, compressedData))
            {
                CQ_CORE_ERROR("Decompression: Failed to read input file: {}", inputPath);
                return false;
            }

            std::vector<uint8_t> decompressedData;
            if (!Decompress(compressedData, decompressedData, algorithm))
            {
                return false;
            }

            if (!FileSystem::WriteFileBinary(outputPath, decompressedData))
            {
                CQ_CORE_ERROR("Decompression: Failed to write output file: {}", outputPath);
                return false;
            }

            CQ_CORE_INFO("Decompressed {} -> {}", inputPath, outputPath);
            return true;
        }

        float GetCompressionRatio(size_t originalSize, size_t compressedSize)
        {
            if (originalSize == 0)
                return 0.0f;
            
            return (1.0f - (float)compressedSize / (float)originalSize) * 100.0f;
        }

        size_t GetMaxCompressedSize(size_t inputSize, CompressionAlgorithm algorithm)
        {
            switch (algorithm)
            {
                case CompressionAlgorithm::Zlib:
                    return compressBound(inputSize);
                
                case CompressionAlgorithm::Zstd:
                    return ZSTD_compressBound(inputSize);
                
                case CompressionAlgorithm::None:
                    return inputSize;
                
                default:
                    return inputSize * 2; // Güvenli tahmin
            }
        }
    }
}
