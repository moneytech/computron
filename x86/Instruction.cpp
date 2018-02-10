/*
 * Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Instruction.h"
#include "vcpu.h"

enum InstructionFormat {
    InvalidFormat,
    MultibyteWithSlash,
    MultibyteWithSubopcode,
    InstructionPrefix,

    __BeginFormatsWithRMByte,
    OP_RM16_reg16,
    OP_reg8_RM8,
    OP_reg16_RM16,
    OP_RM16_seg,
    OP_RM32_seg,
    OP_RM8_imm8,
    OP_RM16_imm16,
    OP_RM16_imm8,
    OP_RM32_imm8,
    OP_RM8,
    OP_RM16,
    OP_RM32,
    OP_RM8_reg8,
    OP_RM32_reg32,
    OP_reg32_RM32,
    OP_RM32_imm32,
    OP_reg16_RM16_imm8,
    OP_reg32_RM32_imm8,
    OP_reg16_RM16_imm16,
    OP_reg32_RM32_imm32,
    OP_reg16_mem16,
    OP_reg32_mem32,
    OP_seg_RM16,
    OP_seg_RM32,
    OP_RM8_1,
    OP_RM16_1,
    OP_RM32_1,
    OP_FAR_mem16,
    OP_FAR_mem32,
    OP_RM8_CL,
    OP_RM16_CL,
    OP_RM32_CL,
    OP_reg32_CR,
    OP_CR_reg32,
    OP_reg16_RM8,
    OP_reg32_RM8,
    OP_reg32_RM16,
    __EndFormatsWithRMByte,

    OP_reg32_imm32,
    OP_AL_imm8,
    OP_AX_imm16,
    OP_EAX_imm32,
    OP_CS,
    OP_DS,
    OP_ES,
    OP_SS,
    OP_FS,
    OP_GS,
    OP,
    OP_reg16,
    OP_imm16,
    OP_imm8,
    OP_imm16_imm16,
    OP_imm16_imm32,
    OP_AX_reg16,
    OP_EAX_reg32,
    OP_AL_moff8,
    OP_AX_moff16,
    OP_EAX_moff32,
    OP_moff8_AL,
    OP_moff16_AX,
    OP_moff32_EAX,
    OP_reg8_imm8,
    OP_reg16_imm16,
    OP_3,
    OP_AX_imm8,
    OP_EAX_imm8,
    OP_short_imm8,
    OP_AL_DX,
    OP_AX_DX,
    OP_EAX_DX,
    OP_DX_AL,
    OP_DX_AX,
    OP_DX_EAX,
    OP_OP,
    OP_imm8_AL,
    OP_imm8_AX,
    OP_imm8_EAX,
    OP_relimm16,
    OP_reg8_CL,

    OP_reg32,
    OP_imm32,
    OP_imm16_imm8,

    OP_NEAR_imm,
};

static const unsigned CurrentAddressSize = 0xB33FBABE;

struct InstructionDescriptor {
    InstructionImpl impl { nullptr };
    QString mnemonic;
    InstructionFormat format { InvalidFormat };
    bool hasRM { false };
    unsigned imm1Bytes { 0 };
    unsigned imm2Bytes { 0 };
    InstructionDescriptor* slashes { nullptr };

    unsigned imm1BytesForAddressSize(bool a32)
    {
        if (imm1Bytes == CurrentAddressSize)
            return a32 ? 4 : 2;
        return imm1Bytes;
    }

    unsigned imm2BytesForAddressSize(bool a32)
    {
        if (imm2Bytes == CurrentAddressSize)
            return a32 ? 4 : 2;
        return imm2Bytes;
    }
};

static InstructionDescriptor s_table16[256];
static InstructionDescriptor s_table32[256];
static InstructionDescriptor s_0F_table16[256];
static InstructionDescriptor s_0F_table32[256];

static void build(InstructionDescriptor* table, BYTE op, const char* mnemonic, InstructionFormat format, InstructionImpl impl)
{
    InstructionDescriptor& d = table[op];
    VM_ASSERT(!d.impl);

    d.mnemonic = mnemonic;
    d.format = format;
    d.impl = impl;

    if ((format > __BeginFormatsWithRMByte && format < __EndFormatsWithRMByte) || format == MultibyteWithSlash)
        d.hasRM = true;

    switch (format) {
    case OP_RM8_imm8:
    case OP_RM16_imm8:
    case OP_RM32_imm8:
    case OP_reg16_RM16_imm8:
    case OP_reg32_RM32_imm8:
    case OP_AL_imm8:
    case OP_imm8:
    case OP_reg8_imm8:
    case OP_AX_imm8:
    case OP_EAX_imm8:
    case OP_short_imm8:
    case OP_imm8_AL:
    case OP_imm8_AX:
    case OP_imm8_EAX:
        d.imm1Bytes = 1;
        break;
    case OP_reg16_RM16_imm16:
    case OP_AX_imm16:
    case OP_imm16:
    case OP_reg16_imm16:
    case OP_relimm16:
    case OP_RM16_imm16:
        d.imm1Bytes = 2;
        break;
    case OP_RM32_imm32:
    case OP_reg32_RM32_imm32:
    case OP_reg32_imm32:
    case OP_EAX_imm32:
    case OP_imm32:
        d.imm1Bytes = 4;
        break;
    case OP_imm16_imm8:
        d.imm1Bytes = 2;
        d.imm2Bytes = 1;
        break;
    case OP_imm16_imm16:
        d.imm1Bytes = 2;
        d.imm2Bytes = 2;
        break;
    case OP_imm16_imm32:
        d.imm1Bytes = 2;
        d.imm2Bytes = 4;
        break;
    case OP_moff8_AL:
    case OP_moff16_AX:
    case OP_moff32_EAX:
    case OP_AL_moff8:
    case OP_AX_moff16:
    case OP_EAX_moff32:
    case OP_NEAR_imm:
        d.imm1Bytes = CurrentAddressSize;
        break;
    //default:
    case InvalidFormat:
    case MultibyteWithSlash:
    case MultibyteWithSubopcode:
    case InstructionPrefix:
    case __BeginFormatsWithRMByte:
    case OP_RM16_reg16:
    case OP_reg8_RM8:
    case OP_reg16_RM16:
    case OP_RM16_seg:
    case OP_RM32_seg:
    case OP_RM8:
    case OP_RM16:
    case OP_RM32:
    case OP_RM8_reg8:
    case OP_RM32_reg32:
    case OP_reg32_RM32:
    case OP_reg16_mem16:
    case OP_reg32_mem32:
    case OP_seg_RM16:
    case OP_seg_RM32:
    case OP_RM8_1:
    case OP_RM16_1:
    case OP_RM32_1:
    case OP_FAR_mem16:
    case OP_FAR_mem32:
    case OP_RM8_CL:
    case OP_RM16_CL:
    case OP_RM32_CL:
    case OP_reg32_CR:
    case OP_CR_reg32:
    case OP_reg16_RM8:
    case OP_reg32_RM8:
    case __EndFormatsWithRMByte:
    case OP_CS:
    case OP_DS:
    case OP_ES:
    case OP_SS:
    case OP_FS:
    case OP_GS:
    case OP:
    case OP_reg16:
    case OP_AX_reg16:
    case OP_EAX_reg32:
    case OP_3:
    case OP_AL_DX:
    case OP_AX_DX:
    case OP_EAX_DX:
    case OP_DX_AL:
    case OP_DX_AX:
    case OP_DX_EAX:
    case OP_OP:
    case OP_reg8_CL:
    case OP_reg32:
    case OP_reg32_RM16:
        break;
    }
}

static void buildSlash(InstructionDescriptor* table, BYTE op, BYTE slash, const char* mnemonic, InstructionFormat format, InstructionImpl impl)
{
    InstructionDescriptor& d = table[op];
    d.format = MultibyteWithSlash;
    d.hasRM = true;
    if (!d.slashes)
        d.slashes = new InstructionDescriptor[8];

    build(d.slashes, slash, mnemonic, format, impl);
}

static void build0F(BYTE op, const char* mnemonic, InstructionFormat format, void (VCpu::*impl)(Instruction&))
{
    build(s_0F_table16, op, mnemonic, format, impl);
    build(s_0F_table32, op, mnemonic, format, impl);
}

static void build(BYTE op, const char* mnemonic, InstructionFormat format, void (VCpu::*impl)(Instruction&))
{
    build(s_table16, op, mnemonic, format, impl);
    build(s_table32, op, mnemonic, format, impl);
}

static void build(BYTE op, const char* mnemonic, InstructionFormat format16, void (VCpu::*impl16)(Instruction&), InstructionFormat format32, void (VCpu::*impl32)(Instruction&))
{
    build(s_table16, op, mnemonic, format16, impl16);
    build(s_table32, op, mnemonic, format32, impl32);
}

static void build0F(BYTE op, const char* mnemonic, InstructionFormat format16, void (VCpu::*impl16)(Instruction&), InstructionFormat format32, void (VCpu::*impl32)(Instruction&))
{
    build(s_0F_table16, op, mnemonic, format16, impl16);
    build(s_0F_table32, op, mnemonic, format32, impl32);
}

static void build(BYTE op, const char* mnemonic16, InstructionFormat format16, void (VCpu::*impl16)(Instruction&), const char* mnemonic32, InstructionFormat format32, void (VCpu::*impl32)(Instruction&))
{
    build(s_table16, op, mnemonic16, format16, impl16);
    build(s_table32, op, mnemonic32, format32, impl32);
}

static void buildSlash(BYTE op, BYTE slash, const char* mnemonic, InstructionFormat format, void (VCpu::*impl)(Instruction&))
{
    buildSlash(s_table16, op, slash, mnemonic, format, impl);
    buildSlash(s_table32, op, slash, mnemonic, format, impl);
}

static void buildSlash(BYTE op, BYTE slash, const char* mnemonic, InstructionFormat format16, void (VCpu::*impl16)(Instruction&), InstructionFormat format32, void (VCpu::*impl32)(Instruction&))
{
    buildSlash(s_table16, op, slash, mnemonic, format16, impl16);
    buildSlash(s_table32, op, slash, mnemonic, format32, impl32);
}

static void build0FSlash(BYTE op, BYTE slash, const char* mnemonic, InstructionFormat format16, void (VCpu::*impl16)(Instruction&), InstructionFormat format32, void (VCpu::*impl32)(Instruction&))
{
    buildSlash(s_0F_table16, op, slash, mnemonic, format16, impl16);
    buildSlash(s_0F_table32, op, slash, mnemonic, format32, impl32);
}

static void build0FSlash(BYTE op, BYTE slash, const char* mnemonic, InstructionFormat format, void (VCpu::*impl)(Instruction&))
{
    buildSlash(s_0F_table16, op, slash, mnemonic, format, impl);
    buildSlash(s_0F_table32, op, slash, mnemonic, format, impl);
}

void buildOpcodeTablesIfNeeded()
{
    static bool hasBuiltTables = false;
    if (hasBuiltTables)
        return;

    build(0x00, "ADD",    OP_RM8_reg8,         &VCpu::_ADD_RM8_reg8);
    build(0x01, "ADD",    OP_RM16_reg16,       &VCpu::_ADD_RM16_reg16,  OP_RM32_reg32,  &VCpu::_ADD_RM32_reg32);
    build(0x02, "ADD",    OP_reg8_RM8,         &VCpu::_ADD_reg8_RM8);
    build(0x03, "ADD",    OP_reg16_RM16,       &VCpu::_ADD_reg16_RM16,  OP_reg32_RM32,  &VCpu::_ADD_reg32_RM32);
    build(0x04, "ADD",    OP_AL_imm8,          &VCpu::_ADD_AL_imm8);
    build(0x05, "ADD",    OP_AX_imm16,         &VCpu::_ADD_AX_imm16,    OP_EAX_imm32,   &VCpu::_ADD_EAX_imm32);
    build(0x06, "PUSH",   OP_ES,               &VCpu::_PUSH_ES);
    build(0x07, "POP",    OP_ES,               &VCpu::_POP_ES);
    build(0x08, "OR",     OP_RM8_reg8,         &VCpu::_OR_RM8_reg8);
    build(0x09, "OR",     OP_RM16_reg16,       &VCpu::_OR_RM16_reg16,   OP_RM32_reg32,  &VCpu::_OR_RM32_reg32);
    build(0x0A, "OR",     OP_reg8_RM8,         &VCpu::_OR_reg8_RM8);
    build(0x0B, "OR",     OP_reg16_RM16,       &VCpu::_OR_reg16_RM16,   OP_reg32_RM32,  &VCpu::_OR_reg32_RM32);
    build(0x0C, "OR",     OP_AL_imm8,          &VCpu::_OR_AL_imm8);
    build(0x0D, "OR",     OP_AX_imm16,         &VCpu::_OR_AX_imm16,     OP_EAX_imm32,   &VCpu::_OR_EAX_imm32);
    build(0x0E, "PUSH",   OP_CS,               &VCpu::_PUSH_CS);

    build(0x10, "ADC",    OP_RM8_reg8,         &VCpu::_ADC_RM8_reg8);
    build(0x11, "ADC",    OP_RM16_reg16,       &VCpu::_ADC_RM16_reg16,  OP_RM32_reg32,  &VCpu::_ADC_RM32_reg32);
    build(0x12, "ADC",    OP_reg8_RM8,         &VCpu::_ADC_reg8_RM8);
    build(0x13, "ADC",    OP_reg16_RM16,       &VCpu::_ADC_reg16_RM16,  OP_reg32_RM32,  &VCpu::_ADC_reg32_RM32);
    build(0x14, "ADC",    OP_AL_imm8,          &VCpu::_ADC_AL_imm8);
    build(0x15, "ADC",    OP_AX_imm16,         &VCpu::_ADC_AX_imm16,    OP_EAX_imm32,   &VCpu::_ADC_EAX_imm32);
    build(0x16, "PUSH",   OP_SS,               &VCpu::_PUSH_SS);
    build(0x17, "POP",    OP_SS,               &VCpu::_POP_SS);
    build(0x18, "SBB",    OP_RM8_reg8,         &VCpu::_SBB_RM8_reg8);
    build(0x19, "SBB",    OP_RM16_reg16,       &VCpu::_SBB_RM16_reg16,  OP_RM32_reg32,  &VCpu::_SBB_RM32_reg32);
    build(0x1A, "SBB",    OP_reg8_RM8,         &VCpu::_SBB_reg8_RM8);
    build(0x1B, "SBB",    OP_reg16_RM16,       &VCpu::_SBB_reg16_RM16,  OP_reg32_RM32,  &VCpu::_SBB_reg32_RM32);
    build(0x1C, "SBB",    OP_AL_imm8,          &VCpu::_SBB_AL_imm8);
    build(0x1D, "SBB",    OP_AX_imm16,         &VCpu::_SBB_AX_imm16,    OP_EAX_imm32,   &VCpu::_SBB_EAX_imm32);
    build(0x1E, "PUSH",   OP_DS,               &VCpu::_PUSH_DS);
    build(0x1F, "POP",    OP_DS,               &VCpu::_POP_DS);

    build(0x20, "AND",    OP_RM8_reg8,         &VCpu::_AND_RM8_reg8);
    build(0x21, "AND",    OP_RM16_reg16,       &VCpu::_AND_RM16_reg16,  OP_RM32_reg32,  &VCpu::_AND_RM32_reg32);
    build(0x22, "AND",    OP_reg8_RM8,         &VCpu::_AND_reg8_RM8);
    build(0x23, "AND",    OP_reg16_RM16,       &VCpu::_AND_reg16_RM16,  OP_reg32_RM32,  &VCpu::_AND_reg32_RM32);
    build(0x24, "AND",    OP_AL_imm8,          &VCpu::_AND_AL_imm8);
    build(0x25, "AND",    OP_AX_imm16,         &VCpu::_AND_AX_imm16,    OP_EAX_imm32,   &VCpu::_AND_EAX_imm32);
    build(0x26, "ES:",    InstructionPrefix,   &VCpu::_ES);
    build(0x27, "DAA",    OP,                  &VCpu::_DAA);
    build(0x28, "SUB",    OP_RM8_reg8,         &VCpu::_SUB_RM8_reg8);
    build(0x29, "SUB",    OP_RM16_reg16,       &VCpu::_SUB_RM16_reg16,  OP_RM32_reg32,  &VCpu::_SUB_RM32_reg32);
    build(0x2A, "SUB",    OP_reg8_RM8,         &VCpu::_SUB_reg8_RM8);
    build(0x2B, "SUB",    OP_reg16_RM16,       &VCpu::_SUB_reg16_RM16,  OP_reg32_RM32,  &VCpu::_SUB_reg32_RM32);
    build(0x2C, "SUB",    OP_AL_imm8,          &VCpu::_SUB_AL_imm8);
    build(0x2D, "SUB",    OP_AX_imm16,         &VCpu::_SUB_AX_imm16,    OP_EAX_imm32,   &VCpu::_SUB_EAX_imm32);
    build(0x2E, "CS:",    InstructionPrefix,   &VCpu::_CS);
    build(0x2F, "DAS",    OP,                  &VCpu::_DAS);

    build(0x30, "XOR",    OP_RM8_reg8,         &VCpu::_XOR_RM8_reg8);
    build(0x31, "XOR",    OP_RM16_reg16,       &VCpu::_XOR_RM16_reg16,  OP_RM32_reg32,  &VCpu::_XOR_RM32_reg32);
    build(0x32, "XOR",    OP_reg8_RM8,         &VCpu::_XOR_reg8_RM8);
    build(0x33, "XOR",    OP_reg16_RM16,       &VCpu::_XOR_reg16_RM16,  OP_reg32_RM32,  &VCpu::_XOR_reg32_RM32);
    build(0x34, "XOR",    OP_AL_imm8,          &VCpu::_XOR_AL_imm8);
    build(0x35, "XOR",    OP_AX_imm16,         &VCpu::_XOR_AX_imm16,    OP_EAX_imm32,   &VCpu::_XOR_EAX_imm32);
    build(0x36, "SS:",    InstructionPrefix,   &VCpu::_SS);
    build(0x37, "AAA",    OP,                  &VCpu::_AAA);
    build(0x38, "CMP",    OP_RM8_reg8,         &VCpu::_CMP_RM8_reg8);
    build(0x39, "CMP",    OP_RM16_reg16,       &VCpu::_CMP_RM16_reg16,  OP_RM32_reg32,  &VCpu::_CMP_RM32_reg32);
    build(0x3A, "CMP",    OP_reg8_RM8,         &VCpu::_CMP_reg8_RM8);
    build(0x3B, "CMP",    OP_reg16_RM16,       &VCpu::_CMP_reg16_RM16,  OP_reg32_RM32,  &VCpu::_CMP_reg32_RM32);
    build(0x3C, "CMP",    OP_AL_imm8,          &VCpu::_CMP_AL_imm8);
    build(0x3D, "CMP",    OP_AX_imm16,         &VCpu::_CMP_AX_imm16,    OP_EAX_imm32,   &VCpu::_CMP_EAX_imm32);
    build(0x3E, "DS:",    InstructionPrefix,   &VCpu::_DS);
    build(0x3F, "AAS",    OP,                  &VCpu::_AAS);

    for (BYTE i = 0; i <= 7; ++i)
        build(0x40 + i, "INC", OP_reg16, &VCpu::_INC_reg16, OP_reg32, &VCpu::_INC_reg32);

    for (BYTE i = 0; i <= 7; ++i)
        build(0x48 + i, "DEC", OP_reg16, &VCpu::_DEC_reg16, OP_reg32, &VCpu::_DEC_reg32);

    for (BYTE i = 0; i <= 7; ++i)
        build(0x50 + i, "PUSH", OP_reg16, &VCpu::_PUSH_reg16, OP_reg32, &VCpu::_PUSH_reg32);

    for (BYTE i = 0; i <= 7; ++i)
        build(0x58 + i, "POP", OP_reg16, &VCpu::_POP_reg16, OP_reg32, &VCpu::_POP_reg32);

    build(0x60, "PUSHAW", OP,                  &VCpu::_PUSHA, "PUSHAD", OP,             &VCpu::_PUSHAD);
    build(0x61, "POPAW",  OP,                  &VCpu::_POPA,  "POPAW",  OP,             &VCpu::_POPAD);
    build(0x62, "BOUND",  OP,                  &VCpu::_BOUND);

    build(0x64, "FS:",    InstructionPrefix,   &VCpu::_FS);
    build(0x65, "GS:",    InstructionPrefix,   &VCpu::_GS);
    build(0x66, "o!",     InstructionPrefix,   &VCpu::_OperationSizeOverride);
    build(0x67, "a!",     InstructionPrefix,   &VCpu::_AddressSizeOverride);

    build(0x68, "PUSH",   OP_imm16,            &VCpu::_PUSH_imm16,      OP_imm32,       &VCpu::_PUSH_imm32);
    build(0x69, "IMUL",   OP_reg16_RM16_imm16, &VCpu::_IMUL_reg16_RM16_imm16, OP_reg32_RM32_imm32, &VCpu::_IMUL_reg32_RM32_imm32);
    build(0x6A, "PUSH",   OP_imm8,             &VCpu::_PUSH_imm8);
    build(0x6B, "IMUL",   OP_reg16_RM16_imm8,  &VCpu::_IMUL_reg16_RM16_imm8, OP_reg32_RM32_imm8, &VCpu::_IMUL_reg32_RM32_imm8);
    build(0x6E, "OUTSB",  OP,                  &VCpu::_OUTSB);
    build(0x6F, "OUTSW",  OP,                  &VCpu::_OUTSW, "OUTSD",  OP,             &VCpu::_OUTSD);

    build(0x70, "JO",     OP_short_imm8,       &VCpu::_JO_imm8);
    build(0x71, "JNO",    OP_short_imm8,       &VCpu::_JNO_imm8);
    build(0x72, "JC",     OP_short_imm8,       &VCpu::_JC_imm8);
    build(0x73, "JNC",    OP_short_imm8,       &VCpu::_JNC_imm8);
    build(0x74, "JZ",     OP_short_imm8,       &VCpu::_JZ_imm8);
    build(0x75, "JNZ",    OP_short_imm8,       &VCpu::_JNZ_imm8);
    build(0x76, "JNA",    OP_short_imm8,       &VCpu::_JNA_imm8);
    build(0x77, "JA",     OP_short_imm8,       &VCpu::_JA_imm8);
    build(0x78, "JS",     OP_short_imm8,       &VCpu::_JS_imm8);
    build(0x79, "JNS",    OP_short_imm8,       &VCpu::_JNS_imm8);
    build(0x7A, "JP",     OP_short_imm8,       &VCpu::_JP_imm8);
    build(0x7B, "JNP",    OP_short_imm8,       &VCpu::_JNP_imm8);
    build(0x7C, "JL",     OP_short_imm8,       &VCpu::_JL_imm8);
    build(0x7D, "JNL",    OP_short_imm8,       &VCpu::_JNL_imm8);
    build(0x7E, "JNG",    OP_short_imm8,       &VCpu::_JNG_imm8);
    build(0x7F, "JG",     OP_short_imm8,       &VCpu::_JG_imm8);

    build(0x84, "TEST",   OP_RM8_reg8,         &VCpu::_TEST_RM8_reg8);
    build(0x85, "TEST",   OP_RM16_reg16,       &VCpu::_TEST_RM16_reg16, OP_RM32_reg32,  &VCpu::_TEST_RM32_reg32);
    build(0x86, "XCHG",   OP_reg8_RM8,         &VCpu::_XCHG_reg8_RM8);
    build(0x87, "XCHG",   OP_reg16_RM16,       &VCpu::_XCHG_reg16_RM16, OP_reg32_RM32,  &VCpu::_XCHG_reg32_RM32);
    build(0x88, "MOV",    OP_RM8_reg8,         &VCpu::_MOV_RM8_reg8);
    build(0x89, "MOV",    OP_RM16_reg16,       &VCpu::_MOV_RM16_reg16,  OP_RM32_reg32,  &VCpu::_MOV_RM32_reg32);
    build(0x8A, "MOV",    OP_reg8_RM8,         &VCpu::_MOV_reg8_RM8);
    build(0x8B, "MOV",    OP_reg16_RM16,       &VCpu::_MOV_reg16_RM16,  OP_reg32_RM32,  &VCpu::_MOV_reg32_RM32);
    build(0x8C, "MOV",    OP_RM16_seg,         &VCpu::_MOV_RM16_seg,    OP_RM32_seg,    &VCpu::_MOV_RM32_seg);
    build(0x8D, "LEA",    OP_reg16_mem16,      &VCpu::_LEA_reg16_mem16, OP_reg32_mem32, &VCpu::_LEA_reg32_mem32);
    build(0x8E, "MOV",    OP_seg_RM16,         &VCpu::_MOV_seg_RM16,    OP_seg_RM32,    &VCpu::_MOV_seg_RM32);

    build(0x90, "NOP", OP, &VCpu::_NOP);

    for (BYTE i = 0; i <= 6; ++i)
        build(0x91 + i, "XCHG", OP_AX_reg16, &VCpu::_XCHG_AX_reg16, OP_EAX_reg32, &VCpu::_XCHG_EAX_reg32);

    build(0x98, "CBW",    OP,                  &VCpu::_CBW,      "CWDE", OP,             &VCpu::_CWDE);
    build(0x99, "CWD",    OP,                  &VCpu::_CWD,       "CDQ", OP,             &VCpu::_CDQ);
    build(0x9A, "CALL",   OP_imm16_imm16,      &VCpu::_CALL_imm16_imm16, OP_imm16_imm32, &VCpu::_CALL_imm16_imm32);
    build(0x9C, "PUSHFW", OP,                  &VCpu::_PUSHF,  "PUSHFD", OP,             &VCpu::_PUSHFD);
    build(0x9D, "POPFW",  OP,                  &VCpu::_POPF,    "POPFD", OP,             &VCpu::_POPFD);
    build(0x9E, "SAHF",   OP,                  &VCpu::_SAHF);
    build(0x9F, "LAHF",   OP,                  &VCpu::_LAHF);

    build(0xA0, "MOV",    OP_AL_moff8,         &VCpu::_MOV_AL_moff8);
    build(0xA1, "MOV",    OP_AX_moff16,        &VCpu::_MOV_AX_moff16,    OP_EAX_moff32,  &VCpu::_MOV_EAX_moff32);
    build(0xA2, "MOV",    OP_moff8_AL,         &VCpu::_MOV_moff8_AL);
    build(0xA3, "MOV",    OP_moff16_AX,        &VCpu::_MOV_moff16_AX,    OP_moff32_EAX,  &VCpu::_MOV_moff32_EAX);
    build(0xA4, "MOVSB",  OP,                  &VCpu::_MOVSB);
    build(0xA5, "MOVSW",  OP,                  &VCpu::_MOVSW,   "MOVSD", OP,             &VCpu::_MOVSD);
    build(0xA6, "CMPSB",  OP,                  &VCpu::_CMPSB);
    build(0xA7, "CMPSW",  OP,                  &VCpu::_CMPSW,   "CMPSD", OP,             &VCpu::_CMPSD);
    build(0xA8, "TEST",   OP_AL_imm8,          &VCpu::_TEST_AL_imm8);
    build(0xA9, "TEST",   OP_AX_imm16,         &VCpu::_TEST_AX_imm16,    OP_EAX_imm32,   &VCpu::_TEST_EAX_imm32);
    build(0xAA, "STOSB",  OP,                  &VCpu::_STOSB);
    build(0xAB, "STOSW",  OP,                  &VCpu::_STOSW,   "STOSD", OP,             &VCpu::_STOSD);
    build(0xAC, "LODSB",  OP,                  &VCpu::_LODSB);
    build(0xAD, "LODSW",  OP,                  &VCpu::_LODSW,   "LODSD", OP,             &VCpu::_LODSD);
    build(0xAE, "SCASB",  OP,                  &VCpu::_SCASB);
    build(0xAF, "SCASW",  OP,                  &VCpu::_SCASW,   "SCASD", OP,             &VCpu::_SCASD);

    build(0xB0, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_AL_imm8);
    build(0xB1, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_CL_imm8);
    build(0xB2, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_DL_imm8);
    build(0xB3, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_BL_imm8);
    build(0xB4, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_AH_imm8);
    build(0xB5, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_CH_imm8);
    build(0xB6, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_DH_imm8);
    build(0xB7, "MOV",    OP_reg8_imm8,        &VCpu::_MOV_BH_imm8);
    build(0xB8, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_AX_imm16,     OP_reg32_imm32, &VCpu::_MOV_EAX_imm32);
    build(0xB9, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_CX_imm16,     OP_reg32_imm32, &VCpu::_MOV_ECX_imm32);
    build(0xBA, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_DX_imm16,     OP_reg32_imm32, &VCpu::_MOV_EDX_imm32);
    build(0xBB, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_BX_imm16,     OP_reg32_imm32, &VCpu::_MOV_EBX_imm32);
    build(0xBC, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_SP_imm16,     OP_reg32_imm32, &VCpu::_MOV_ESP_imm32);
    build(0xBD, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_BP_imm16,     OP_reg32_imm32, &VCpu::_MOV_EBP_imm32);
    build(0xBE, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_SI_imm16,     OP_reg32_imm32, &VCpu::_MOV_ESI_imm32);
    build(0xBF, "MOV",    OP_reg16_imm16,      &VCpu::_MOV_DI_imm16,     OP_reg32_imm32, &VCpu::_MOV_EDI_imm32);

    build(0xC2, "RET",    OP_imm16,            &VCpu::_RET_imm16);
    build(0xC3, "RET",    OP,                  &VCpu::_RET);
    build(0xC4, "LES",    OP_reg16_mem16,      &VCpu::_LES_reg16_mem16,  OP_reg32_mem32, &VCpu::_LES_reg32_mem32);
    build(0xC5, "LDS",    OP_reg16_mem16,      &VCpu::_LDS_reg16_mem16,  OP_reg32_mem32, &VCpu::_LDS_reg32_mem32);
    build(0xC6, "MOV",    OP_RM8_imm8,         &VCpu::_MOV_RM8_imm8);
    build(0xC7, "MOV",    OP_RM16_imm16,       &VCpu::_MOV_RM16_imm16,   OP_RM32_imm32,  &VCpu::_MOV_RM32_imm32);
    build(0xC8, "ENTER",  OP_imm16_imm8,       &VCpu::_ENTER);
    build(0xC9, "LEAVE",  OP,                  &VCpu::_LEAVE);
    build(0xCA, "RETF",   OP_imm16,            &VCpu::_RETF_imm16);
    build(0xCB, "RETF",   OP,                  &VCpu::_RETF);
    build(0xCC, "INT3",   OP_3 ,               &VCpu::_INT3);
    build(0xCD, "INT",    OP_imm8,             &VCpu::_INT_imm8);
    build(0xCF, "IRET",   OP,                  &VCpu::_IRET);

    build(0xD4, "AAM",    OP_imm8,             &VCpu::_AAM);
    build(0xD5, "AAD",    OP_imm8,             &VCpu::_AAD);
    build(0xD6, "SALC",   OP,                  &VCpu::_SALC);
    build(0xD7, "XLAT",   OP,                  &VCpu::_XLAT);

    // FIXME: D8-DF == FPU
    for (BYTE i = 0; i <= 7; ++i)
        build(0xD8 + i, "FPU?",   OP_RM8,              &VCpu::_ESCAPE);

    build(0xE0, "LOOPNZ", OP_imm8,             &VCpu::_LOOPNE_imm8);
    build(0xE1, "LOOPZ",  OP_imm8,             &VCpu::_LOOPE_imm8);
    build(0xE2, "LOOP",   OP_imm8,             &VCpu::_LOOP_imm8);
    build(0xE3, "JCXZ",   OP_imm8,             &VCpu::_JCXZ_imm8);
    build(0xE4, "IN",     OP_AL_imm8,          &VCpu::_IN_AL_imm8);
    build(0xE5, "IN",     OP_AX_imm8,          &VCpu::_IN_AX_imm8,       OP_EAX_imm8,    &VCpu::_IN_EAX_imm8);
    build(0xE6, "OUT",    OP_imm8_AL,          &VCpu::_OUT_imm8_AL);
    build(0xE7, "OUT",    OP_imm8_AX,          &VCpu::_OUT_imm8_AX,      OP_imm8_EAX,    &VCpu::_OUT_imm8_EAX);
    build(0xE8, "CALL",   OP_imm16,            &VCpu::_CALL_imm16,       OP_imm32,       &VCpu::_CALL_imm32);
    build(0xE9, "JMP",    OP_imm16,            &VCpu::_JMP_imm16,        OP_imm32,       &VCpu::_JMP_imm32);
    build(0xEA, "JMP",    OP_imm16_imm16,      &VCpu::_JMP_imm16_imm16,  OP_imm16_imm32, &VCpu::_JMP_imm16_imm32);
    build(0xEB, "JMP",    OP_short_imm8,       &VCpu::_JMP_short_imm8);
    build(0xEC, "IN",     OP_AL_DX,            &VCpu::_IN_AL_DX);
    build(0xED, "IN",     OP_AX_DX,            &VCpu::_IN_AX_DX,         OP_EAX_DX,      &VCpu::_IN_EAX_DX);
    build(0xEE, "OUT",    OP_DX_AL,            &VCpu::_OUT_DX_AL);
    build(0xEF, "OUT",    OP_DX_AX,            &VCpu::_OUT_DX_AX,        OP_DX_EAX,      &VCpu::_OUT_DX_EAX);

    // F0 = LOCK

    build(0xF1, "VKILL",  OP,                  &VCpu::_VKILL);

    build(0xF2, "REPNZ",  InstructionPrefix,   &VCpu::_REPNE);
    build(0xF3, "REP",    InstructionPrefix,   &VCpu::_REP);
    build(0xF4, "HLT",    OP,                  &VCpu::_HLT);
    build(0xF5, "CMC",    OP,                  &VCpu::_CMC);

    build(0xF8, "CLC",    OP,                  &VCpu::_CLC);
    build(0xF9, "STC",    OP,                  &VCpu::_STC);
    build(0xFA, "CLI",    OP,                  &VCpu::_CLI);
    build(0xFB, "STI",    OP,                  &VCpu::_STI);
    build(0xFC, "CLI",    OP,                  &VCpu::_CLD);
    build(0xFD, "STI",    OP,                  &VCpu::_STD);

    buildSlash(0x80, 0, "ADD",   OP_RM8_imm8,   &VCpu::_ADD_RM8_imm8);
    buildSlash(0x80, 1, "OR",    OP_RM8_imm8,   &VCpu::_OR_RM8_imm8);
    buildSlash(0x80, 2, "ADC",   OP_RM8_imm8,   &VCpu::_ADC_RM8_imm8);
    buildSlash(0x80, 3, "SBB",   OP_RM8_imm8,   &VCpu::_SBB_RM8_imm8);
    buildSlash(0x80, 4, "AND",   OP_RM8_imm8,   &VCpu::_AND_RM8_imm8);
    buildSlash(0x80, 5, "SUB",   OP_RM8_imm8,   &VCpu::_SUB_RM8_imm8);
    buildSlash(0x80, 6, "XOR",   OP_RM8_imm8,   &VCpu::_XOR_RM8_imm8);
    buildSlash(0x80, 7, "CMP",   OP_RM8_imm8,   &VCpu::_CMP_RM8_imm8);

    buildSlash(0x81, 0, "ADD",   OP_RM16_imm16, &VCpu::_ADD_RM16_imm16,  OP_RM32_imm32, &VCpu::_ADD_RM32_imm32);
    buildSlash(0x81, 1, "OR",    OP_RM16_imm16, &VCpu::_OR_RM16_imm16,   OP_RM32_imm32, &VCpu::_OR_RM32_imm32);
    buildSlash(0x81, 2, "ADC",   OP_RM16_imm16, &VCpu::_ADC_RM16_imm16,  OP_RM32_imm32, &VCpu::_ADC_RM32_imm32);
    buildSlash(0x81, 3, "SBB",   OP_RM16_imm16, &VCpu::_SBB_RM16_imm16,  OP_RM32_imm32, &VCpu::_SBB_RM32_imm32);
    buildSlash(0x81, 4, "AND",   OP_RM16_imm16, &VCpu::_AND_RM16_imm16,  OP_RM32_imm32, &VCpu::_AND_RM32_imm32);
    buildSlash(0x81, 5, "SUB",   OP_RM16_imm16, &VCpu::_SUB_RM16_imm16,  OP_RM32_imm32, &VCpu::_SUB_RM32_imm32);
    buildSlash(0x81, 6, "XOR",   OP_RM16_imm16, &VCpu::_XOR_RM16_imm16,  OP_RM32_imm32, &VCpu::_XOR_RM32_imm32);
    buildSlash(0x81, 7, "CMP",   OP_RM16_imm16, &VCpu::_CMP_RM16_imm16,  OP_RM32_imm32, &VCpu::_CMP_RM32_imm32);

    buildSlash(0x83, 0, "ADD",   OP_RM16_imm8,  &VCpu::_ADD_RM16_imm8,   OP_RM32_imm8,  &VCpu::_ADD_RM32_imm8);
    buildSlash(0x83, 1, "OR",    OP_RM16_imm8,  &VCpu::_OR_RM16_imm8,    OP_RM32_imm8,  &VCpu::_OR_RM32_imm8);
    buildSlash(0x83, 2, "ADC",   OP_RM16_imm8,  &VCpu::_ADC_RM16_imm8,   OP_RM32_imm8,  &VCpu::_ADC_RM32_imm8);
    buildSlash(0x83, 3, "SBB",   OP_RM16_imm8,  &VCpu::_SBB_RM16_imm8,   OP_RM32_imm8,  &VCpu::_SBB_RM32_imm8);
    buildSlash(0x83, 4, "AND",   OP_RM16_imm8,  &VCpu::_AND_RM16_imm8,   OP_RM32_imm8,  &VCpu::_AND_RM32_imm8);
    buildSlash(0x83, 5, "SUB",   OP_RM16_imm8,  &VCpu::_SUB_RM16_imm8,   OP_RM32_imm8,  &VCpu::_SUB_RM32_imm8);
    buildSlash(0x83, 6, "XOR",   OP_RM16_imm8,  &VCpu::_XOR_RM16_imm8,   OP_RM32_imm8,  &VCpu::_XOR_RM32_imm8);
    buildSlash(0x83, 7, "CMP",   OP_RM16_imm8,  &VCpu::_CMP_RM16_imm8,   OP_RM32_imm8,  &VCpu::_CMP_RM32_imm8);

    buildSlash(0x8F, 0, "POP",   OP_RM16,       &VCpu::_POP_RM16,        OP_RM32,       &VCpu::_POP_RM32);

    buildSlash(0xC0, 0, "ROL",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 1, "ROR",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 2, "RCL",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 3, "RCR",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 4, "SHL",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 5, "SHR",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);
    buildSlash(0xC0, 7, "SAR",   OP_RM8_imm8,   &VCpu::_wrap_0xC0);

    buildSlash(0xC1, 0, "ROL",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 1, "ROR",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 2, "RCL",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 3, "RCR",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 4, "SHL",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 5, "SHR",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);
    buildSlash(0xC1, 7, "SAR",   OP_RM16_imm8,  &VCpu::_wrap_0xC1_16,    OP_RM32_imm8,  &VCpu::_wrap_0xC1_32);

    buildSlash(0xD0, 0, "ROL",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 1, "ROR",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 2, "RCL",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 3, "RCR",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 4, "SHL",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 5, "SHR",   OP_RM8_1,      &VCpu::_wrap_0xD0);
    buildSlash(0xD0, 7, "SAR",   OP_RM8_1,      &VCpu::_wrap_0xD0);

    buildSlash(0xD1, 0, "ROL",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 1, "ROR",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 2, "RCL",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 3, "RCR",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 4, "SHL",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 5, "SHR",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);
    buildSlash(0xD1, 7, "SAR",   OP_RM16_1,     &VCpu::_wrap_0xD1_16,    OP_RM32_1,     &VCpu::_wrap_0xD1_32);

    buildSlash(0xD2, 0, "ROL",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 1, "ROR",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 2, "RCL",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 3, "RCR",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 4, "SHL",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 5, "SHR",   OP_RM8_CL,     &VCpu::_wrap_0xD2);
    buildSlash(0xD2, 7, "SAR",   OP_RM8_CL,     &VCpu::_wrap_0xD2);

    buildSlash(0xD3, 0, "ROL",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 1, "ROR",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 2, "RCL",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 3, "RCR",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 4, "SHL",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 5, "SHR",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);
    buildSlash(0xD3, 7, "SAR",   OP_RM16_CL,    &VCpu::_wrap_0xD3_16,    OP_RM32_CL,    &VCpu::_wrap_0xD3_32);

    buildSlash(0xF6, 0, "TEST",  OP_RM8_imm8,   &VCpu::_TEST_RM8_imm8);
    buildSlash(0xF6, 2, "NOT",   OP_RM8,        &VCpu::_NOT_RM8);
    buildSlash(0xF6, 3, "NEG",   OP_RM8,        &VCpu::_NEG_RM8);
    buildSlash(0xF6, 4, "MUL",   OP_RM8,        &VCpu::_MUL_RM8);
    buildSlash(0xF6, 5, "IMUL",  OP_RM8,        &VCpu::_IMUL_RM8);
    buildSlash(0xF6, 6, "DIV",   OP_RM8,        &VCpu::_DIV_RM8);
    buildSlash(0xF6, 7, "IDIV",  OP_RM8,        &VCpu::_IDIV_RM8);

    buildSlash(0xF7, 0, "TEST",  OP_RM16_imm16, &VCpu::_TEST_RM16_imm16, OP_RM32_imm32, &VCpu::_TEST_RM32_imm32);
    buildSlash(0xF7, 2, "NOT",   OP_RM16,       &VCpu::_NOT_RM16,        OP_RM32,       &VCpu::_NOT_RM32);
    buildSlash(0xF7, 3, "NEG",   OP_RM16,       &VCpu::_NEG_RM16,        OP_RM32,       &VCpu::_NEG_RM32);
    buildSlash(0xF7, 4, "MUL",   OP_RM16,       &VCpu::_MUL_RM16,        OP_RM32,       &VCpu::_MUL_RM32);
    buildSlash(0xF7, 5, "IMUL",  OP_RM16,       &VCpu::_IMUL_RM16,       OP_RM32,       &VCpu::_IMUL_RM32);
    buildSlash(0xF7, 6, "DIV",   OP_RM16,       &VCpu::_DIV_RM16,        OP_RM32,       &VCpu::_DIV_RM32);
    buildSlash(0xF7, 7, "IDIV",  OP_RM16,       &VCpu::_IDIV_RM16,       OP_RM32,       &VCpu::_IDIV_RM32);

    buildSlash(0xFE, 0, "INC",   OP_RM8,        &VCpu::_INC_RM8);
    buildSlash(0xFE, 1, "DEC",   OP_RM8,        &VCpu::_DEC_RM8);

    buildSlash(0xFF, 0, "INC",   OP_RM16,       &VCpu::_INC_RM16,       OP_RM32,       &VCpu::_INC_RM32);
    buildSlash(0xFF, 1, "DEC",   OP_RM16,       &VCpu::_DEC_RM16,       OP_RM32,       &VCpu::_DEC_RM32);
    buildSlash(0xFF, 2, "CALL",  OP_RM16,       &VCpu::_CALL_RM16,      OP_RM32,       &VCpu::_CALL_RM32);
    buildSlash(0xFF, 3, "CALL",  OP_FAR_mem16,  &VCpu::_CALL_FAR_mem16, OP_FAR_mem32,  &VCpu::_CALL_FAR_mem32);
    buildSlash(0xFF, 4, "JMP",   OP_RM16,       &VCpu::_JMP_RM16,       OP_RM32,       &VCpu::_JMP_RM32);
    buildSlash(0xFF, 5, "JMP",   OP_FAR_mem16,  &VCpu::_JMP_FAR_mem16,  OP_FAR_mem32,  &VCpu::_JMP_FAR_mem32);
    buildSlash(0xFF, 6, "PUSH",  OP_RM16,       &VCpu::_PUSH_RM16,      OP_RM32,       &VCpu::_PUSH_RM32);

    // Instructions starting with 0x0F are multi-byte opcodes.
    build0FSlash(0x00, 0, "SLDT",  OP_RM16,      &VCpu::_SLDT_RM16);
    build0FSlash(0x00, 2, "LLDT",  OP_RM16,      &VCpu::_LLDT_RM16);
    build0FSlash(0x00, 3, "LTR",   OP_RM16,      &VCpu::_LTR_RM16);

    build0FSlash(0x01, 0, "SGDT",  OP,           &VCpu::_SGDT);
    build0FSlash(0x01, 1, "SIDT",  OP,           &VCpu::_SIDT);
    build0FSlash(0x01, 2, "LGDT",  OP,           &VCpu::_LGDT);
    build0FSlash(0x01, 3, "LIDT",  OP,           &VCpu::_LIDT);
    build0FSlash(0x01, 4, "SMSW",  OP_RM16,      &VCpu::_SMSW_RM16);
    build0FSlash(0x01, 6, "LMSW",  OP_RM16,      &VCpu::_LMSW_RM16);

    build0FSlash(0xBA, 4, "BT",    OP_RM16_imm8, &VCpu::_BT_RM16_imm8,  OP_RM32_imm8, &VCpu::_BT_RM32_imm8);
    build0FSlash(0xBA, 5, "BTS",   OP_RM16_imm8, &VCpu::_BTS_RM16_imm8, OP_RM32_imm8, &VCpu::_BTS_RM32_imm8);
    build0FSlash(0xBA, 6, "BTR",   OP_RM16_imm8, &VCpu::_BTR_RM16_imm8, OP_RM32_imm8, &VCpu::_BTR_RM32_imm8);
    build0FSlash(0xBA, 7, "BTC",   OP_RM16_imm8, &VCpu::_BTC_RM16_imm8, OP_RM32_imm8, &VCpu::_BTC_RM32_imm8);

    build0F(0x20, "MOV",   OP_reg32_CR,    &VCpu::_MOV_reg32_CR);
    build0F(0x22, "MOV",   OP_CR_reg32,    &VCpu::_MOV_CR_reg32);

    build0F(0x80, "JO",    OP_NEAR_imm,    &VCpu::_JO_NEAR_imm);
    build0F(0x81, "JNO",   OP_NEAR_imm,    &VCpu::_JNO_NEAR_imm);
    build0F(0x82, "JC",    OP_NEAR_imm,    &VCpu::_JC_NEAR_imm);
    build0F(0x83, "JNC",   OP_NEAR_imm,    &VCpu::_JNC_NEAR_imm);
    build0F(0x84, "JZ",    OP_NEAR_imm,    &VCpu::_JZ_NEAR_imm);
    build0F(0x85, "JNZ",   OP_NEAR_imm,    &VCpu::_JNZ_NEAR_imm);
    build0F(0x86, "JNA",   OP_NEAR_imm,    &VCpu::_JNA_NEAR_imm);
    build0F(0x87, "JA",    OP_NEAR_imm,    &VCpu::_JA_NEAR_imm);
    build0F(0x88, "JS",    OP_NEAR_imm,    &VCpu::_JS_NEAR_imm);
    build0F(0x89, "JNS",   OP_NEAR_imm,    &VCpu::_JNS_NEAR_imm);
    build0F(0x8A, "JP",    OP_NEAR_imm,    &VCpu::_JP_NEAR_imm);
    build0F(0x8B, "JNP",   OP_NEAR_imm,    &VCpu::_JNP_NEAR_imm);
    build0F(0x8C, "JL",    OP_NEAR_imm,    &VCpu::_JL_NEAR_imm);
    build0F(0x8D, "JNL",   OP_NEAR_imm,    &VCpu::_JNL_NEAR_imm);
    build0F(0x8E, "JNG",   OP_NEAR_imm,    &VCpu::_JNG_NEAR_imm);
    build0F(0x8F, "JG",    OP_NEAR_imm,    &VCpu::_JG_NEAR_imm);

    build0F(0xA0, "PUSH",  OP_FS,          &VCpu::_PUSH_FS);
    build0F(0xA1, "POP",   OP_FS,          &VCpu::_POP_FS);
    build0F(0xA2, "CPUID", OP,             &VCpu::_CPUID);
    build0F(0xA3, "BT",    OP_RM16_reg16,  &VCpu::_BT_RM16_reg16,   OP_RM32_reg32,  &VCpu::_BT_RM32_reg32);
    build0F(0xA8, "PUSH",  OP_GS,          &VCpu::_PUSH_GS);
    build0F(0xA9, "POP",   OP_GS,          &VCpu::_POP_GS);
    build0F(0xAB, "BTS",   OP_RM16_reg16,  &VCpu::_BTS_RM16_reg16,  OP_RM32_reg32,  &VCpu::_BTS_RM32_reg32);
    build0F(0xAF, "IMUL",  OP_reg16_RM16,  &VCpu::_IMUL_reg16_RM16, OP_reg32_RM32,  &VCpu::_IMUL_reg32_RM32);
    build0F(0xB2, "LSS",   OP_reg16_mem16, &VCpu::_LSS_reg16_mem16, OP_reg32_mem32, &VCpu::_LSS_reg32_mem32);
    build0F(0xB4, "LFS",   OP_reg16_mem16, &VCpu::_LFS_reg16_mem16, OP_reg32_mem32, &VCpu::_LFS_reg32_mem32);
    build0F(0xB5, "LGS",   OP_reg16_mem16, &VCpu::_LGS_reg16_mem16, OP_reg32_mem32, &VCpu::_LGS_reg32_mem32);
    build0F(0xB6, "MOV",   OP_reg16_RM8,   &VCpu::_MOVZX_reg16_RM8, OP_reg32_RM8,   &VCpu::_MOVZX_reg32_RM8);
    build0F(0xB7, "0xB7",  OP,             &VCpu::_UNSUPP,          OP_reg32_RM16,  &VCpu::_MOVZX_reg32_RM16);
    build0F(0xBB, "BTC",   OP_RM16_reg16,  &VCpu::_BTC_RM16_reg16,  OP_RM32_reg32,  &VCpu::_BTC_RM32_reg32);

    hasBuiltTables = true;
}

Instruction Instruction::fromStream(InstructionStream& stream)
{
    return Instruction(stream);
}

static bool opcodeHasRegisterIndex(BYTE op)
{
    // FIXME: Turn this into a lookup table.
    if (op >= 0x40 && op <= 0x5F)
        return true;
    if (op >= 0x90 && op <= 0x97)
        return true;
    return false;
}

Instruction::Instruction(InstructionStream& stream)
{
    bool a32 = stream.a32();
    m_op = stream.readInstruction8();
    InstructionDescriptor* desc;

    if (m_op == 0x0F) {
        m_hasSubOp = true;
        m_subOp = stream.readInstruction8();
        desc = stream.o32() ? &s_0F_table32[m_subOp] : &s_0F_table16[m_subOp];
    } else {
        desc = stream.o32() ? &s_table32[m_op] : &s_table16[m_op];
    }

    m_hasRM = desc->hasRM;
    if (m_hasRM) {
        // Consume ModR/M (may include SIB and displacement.)
        m_modrm.decode(stream);
    } else {
        if (opcodeHasRegisterIndex(m_op)) {
            m_registerIndex = m_op & 7;
        }
    }

    bool hasSlash = desc->format == MultibyteWithSlash;

    if (hasSlash) {
        desc = &desc->slashes[slash()];
    }

    m_impl = desc->impl;
    if (!m_impl) {
        if (m_hasSubOp) {
            if (hasSlash)
                vlog(LogCPU, "Instruction %02X %02X /%u not understood", m_op, m_subOp, slash());
            else
                vlog(LogCPU, "Instruction %02X %02X not understood", m_op, m_subOp);
        } else {
            if (hasSlash)
                vlog(LogCPU, "Instruction %02X /%u not understood", m_op, slash());
            else
                vlog(LogCPU, "Instruction %02X not understood", m_op);
        }
    }

    m_imm1Bytes = desc->imm1BytesForAddressSize(a32);
    m_imm2Bytes = desc->imm2BytesForAddressSize(a32);

    // Consume immediates if present.
    if (m_imm2Bytes)
        m_imm2 = stream.readBytes(m_imm2Bytes);
    if (m_imm1Bytes)
        m_imm1 = stream.readBytes(m_imm1Bytes);
}

unsigned Instruction::registerIndex() const
{
    if (m_hasRM)
        return vomit_modRMRegisterPart(m_modrm.m_rm);
    return m_registerIndex;
}

BYTE& Instruction::reg8()
{
    VM_ASSERT(m_cpu);
    return *m_cpu->treg8[registerIndex()];
}

WORD& Instruction::reg16()
{
    VM_ASSERT(m_cpu);
    VM_ASSERT(m_cpu->o16());
    return *m_cpu->treg16[registerIndex()];
}

WORD& Instruction::segreg()
{
    VM_ASSERT(m_cpu);
    return *m_cpu->m_segmentMap[registerIndex()];
}

DWORD& Instruction::reg32()
{
    VM_ASSERT(m_cpu);
    VM_ASSERT(m_cpu->o32());
    return *m_cpu->treg32[registerIndex()];
}

void Instruction::execute(VCpu& cpu)
{
    m_cpu = &cpu;
    if (m_hasRM)
        m_modrm.resolve(cpu);
    (cpu.*m_impl)(*this);
}

DWORD InstructionStream::readBytes(unsigned count)
{
    switch (count) {
    case 1: return readInstruction8();
    case 2: return readInstruction16();
    case 4: return readInstruction32();
    }
    VM_ASSERT(false);
    return 0;
}