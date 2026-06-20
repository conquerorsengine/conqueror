#pragma once

#include "CQSOpCode.h"
#include "CQSValue.h"

#include <vector>
#include <cstdint>
#include <string>

namespace Conqueror::CQS
{
    // Bytecode container — bir fonksiyonun veya script'in derlenmiş kodu
    struct CQSChunk
    {
        std::vector<uint8_t>    Code;       // Bytecode instruction'ları
        std::vector<Value>      Constants;  // Constant pool
        std::vector<int>        Lines;      // Her instruction için satır numarası (debug)
        std::string             Name;       // Chunk adı (fonksiyon adı, script adı)

        // ── Bytecode Yazma ──
        void WriteByte(uint8_t byte, int line)
        {
            Code.push_back(byte);
            Lines.push_back(line);
        }

        void WriteOpCode(OpCode op, int line)
        {
            WriteByte(static_cast<uint8_t>(op), line);
        }

        // 2 byte operand yaz (16-bit, big-endian)
        void WriteShort(uint16_t value, int line)
        {
            WriteByte((value >> 8) & 0xFF, line);
            WriteByte(value & 0xFF, line);
        }

        // ── Constant Pool ──
        // Constant ekle, index döndür
        int AddConstant(Value value)
        {
            Constants.push_back(value);
            return static_cast<int>(Constants.size() - 1);
        }


        // Constant push instruction yaz
        void WriteConstant(Value value, int line)
        {
            int index = AddConstant(value);
            if (index <= 255)

            {
                WriteOpCode(OpCode::Constant, line);
                WriteByte(index, line);
            }
            else
            {
                WriteOpCode(OpCode::ConstantLong, line);
                WriteShort(static_cast<uint16_t>(Constants.size() - 1), line);
            }
        }

        // ── Jump Patching ──
        // Jump instruction yaz, offset'i sonra doldurulacak — placeholder index döndürür
        int EmitJump(OpCode jumpOp, int line)
        {
            WriteOpCode(jumpOp, line);
            WriteByte(0xFF, line);   // Placeholder high byte
            WriteByte(0xFF, line);   // Placeholder low byte
            return static_cast<int>(Code.size()) - 2;
        }

        // Placeholder'ı gerçek offset ile doldur
        void PatchJump(int offset)
        {
            int jump = static_cast<int>(Code.size()) - offset - 2;
            Code[offset]     = (jump >> 8) & 0xFF;
            Code[offset + 1] = jump & 0xFF;
        }

        // Loop: geriye doğru jump
        void EmitLoop(int loopStart, int line)
        {
            WriteOpCode(OpCode::Loop, line);
            int offset = static_cast<int>(Code.size()) - loopStart + 2;
            WriteByte((offset >> 8) & 0xFF, line);
            WriteByte(offset & 0xFF, line);
        }

        // ── Debug: Disassembly ──
        void Disassemble(const std::string& name) const;
        int  DisassembleInstruction(int offset) const;

        // ── Utility ──
        int Size() const { return static_cast<int>(Code.size()); }
    };
}
