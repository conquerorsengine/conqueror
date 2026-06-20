#include "CQSChunk.h"
#include <cstdio>

namespace Conqueror::CQS
{
    void CQSChunk::Disassemble(const std::string& name) const
    {
        printf("== %s ==\n", name.c_str());
        int offset = 0;
        while (offset < static_cast<int>(Code.size()))
        {
            offset = DisassembleInstruction(offset);
        }
    }

    int CQSChunk::DisassembleInstruction(int offset) const
    {
        printf("%04d ", offset);

        // Satır numarası
        if (offset > 0 && Lines[offset] == Lines[offset - 1])
            printf("   | ");
        else
            printf("%4d ", Lines[offset]);

        OpCode instruction = static_cast<OpCode>(Code[offset]);
        auto info = GetOpCodeInfo(instruction);

        switch (info.OperandBytes)
        {
            case 0:
                printf("%s\n", info.Name);
                return offset + 1;

            case 1:
            {
                uint8_t operand = Code[offset + 1];
                printf("%-20s %4d", info.Name, operand);

                // Constant ise değeri göster
                if (instruction == OpCode::Constant ||
                    instruction == OpCode::GetGlobal ||
                    instruction == OpCode::SetGlobal ||
                    instruction == OpCode::DefineGlobal ||
                    instruction == OpCode::GetProperty ||
                    instruction == OpCode::SetProperty ||
                    instruction == OpCode::NewInstance)
                {
                    if (operand < Constants.size())
                    {
                        printf(" '%s'", Constants[operand].ToString().c_str());
                    }
                }
                printf("\n");
                return offset + 2;
            }

            case 2:
            {
                uint16_t operand = (uint16_t)(Code[offset + 1] << 8) | Code[offset + 2];
                printf("%-20s %4d\n", info.Name, operand);
                return offset + 3;
            }

            default:
                printf("Unknown opcode %d\n", static_cast<int>(instruction));
                return offset + 1;
        }
    }
}
