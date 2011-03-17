/*
 * Copyright (C) 2003-2011 Andreas Kling <kling@webkit.org>
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

#ifndef __templates_h__
#define __templates_h__

#define DEFAULT_RM8_reg8(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                BYTE value = readModRM8(rm); \
                updateModRM8(helper ## 8(this, value, *treg8[vomit_modRMRegisterPart(rm)])); \
	}

#define DEFAULT_RM16_reg16(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                WORD value = readModRM16(rm); \
                updateModRM16(helper ## 16(this, value, *treg16[vomit_modRMRegisterPart(rm)])); \
	}

#define DEFAULT_reg8_RM8(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                BYTE value = readModRM8(rm); \
                *treg8[vomit_modRMRegisterPart(rm)] = helper ## 8(this, *treg8[vomit_modRMRegisterPart(rm)], value); \
	}

#define DEFAULT_reg16_RM16(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                WORD value = readModRM16(rm); \
                *treg16[vomit_modRMRegisterPart(rm)] = helper ## 16(this, *treg16[vomit_modRMRegisterPart(rm)], value); \
	}

#define DEFAULT_reg32_RM32(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                DWORD value = readModRM32(rm); \
                *treg32[vomit_modRMRegisterPart(rm)] = helper ## 32(this, *treg32[vomit_modRMRegisterPart(rm)], value); \
        }

#define DEFAULT_RM8_imm8(helper, name) \
        void VCpu::name() { \
                BYTE value = readModRM8(rmbyte); \
                updateModRM8(helper ## 8(this, value, fetchOpcodeByte())); \
	}

#define DEFAULT_RM16_imm16(helper, name) \
        void VCpu::name() { \
                WORD value = readModRM16(rmbyte); \
                updateModRM16(helper ## 16(this, value, fetchOpcodeWord())); \
	}

#define DEFAULT_RM32_imm32(helper, name) \
        void VCpu::name() { \
                DWORD value = readModRM32(rmbyte); \
                updateModRM32(helper ## 32(this, value, fetchOpcodeDWord())); \
	}

#define DEFAULT_RM16_imm8(helper, name) \
        void VCpu::name() { \
                WORD value = readModRM16(rmbyte); \
                updateModRM16(helper ## 16(this, value, vomit_signExtend(fetchOpcodeByte()))); \
	}

#define DEFAULT_RM32_imm8(helper, name) \
        void VCpu::name() { \
                DWORD value = readModRM32(rmbyte); \
                updateModRM32(helper ## 32(this, value, vomit_signExtend(fetchOpcodeByte()))); \
        }

#define DEFAULT_AL_imm8(helper, name) \
        void VCpu::name() { \
                regs.B.AL = helper ## 8(this, getAL(), fetchOpcodeByte()); \
	}

#define DEFAULT_AX_imm16(helper, name) \
        void VCpu::name() { \
                regs.W.AX = helper ## 16(this, getAX(), fetchOpcodeWord()); \
	}

#define DEFAULT_EAX_imm32(helper, name) \
        void VCpu::name() { \
                regs.D.EAX = helper ## 32(this, getEAX(), fetchOpcodeDWord()); \
        }

#define READONLY_RM8_reg8(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 8(this, readModRM8(rm), *treg8[vomit_modRMRegisterPart(rm)]); \
	}

#define READONLY_RM16_reg16(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 16(this, readModRM16(rm), *treg16[vomit_modRMRegisterPart(rm)]); \
	}

#define READONLY_RM32_reg32(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 32(this, readModRM32(rm), *treg32[vomit_modRMRegisterPart(rm)]); \
        }

#define READONLY_reg8_RM8(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 8(this, *treg8[vomit_modRMRegisterPart(rm)], readModRM8(rm)); \
	}

#define READONLY_reg16_RM16(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 16(this, *treg16[vomit_modRMRegisterPart(rm)], readModRM16(rm)); \
	}

#define READONLY_reg32_RM32(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                helper ## 32(this, *treg32[vomit_modRMRegisterPart(rm)], readModRM32(rm)); \
        }

#define READONLY_RM8_imm8(helper, name) \
        void VCpu::name() { \
                BYTE value = readModRM8(rmbyte); \
                helper ## 8(this, value, fetchOpcodeByte()); \
	}

#define READONLY_RM16_imm16( helper, name ) \
        void VCpu::name() { \
                WORD value = readModRM16(rmbyte); \
                helper ## 16(this, value, fetchOpcodeWord()); \
	}

#define READONLY_RM32_imm8( helper, name ) \
    void VCpu::name() { \
            DWORD value = readModRM32(rmbyte); \
            helper ## 32(this, value, vomit_signExtend(fetchOpcodeByte())); \
    }

#define READONLY_RM32_imm32( helper, name ) \
        void VCpu::name() { \
                DWORD value = readModRM32(rmbyte); \
                helper ## 32(this, value, fetchOpcodeDWord()); \
	}

#define READONLY_RM16_imm8(helper, name) \
        void VCpu::name() { \
                WORD value = readModRM16(rmbyte); \
                helper ## 16(this, value, vomit_signExtend(fetchOpcodeByte())); \
	}

#define READONLY_AL_imm8(helper, name) \
        void VCpu::name() { \
                helper ## 8(this, getAL(), fetchOpcodeByte()); \
	}

#define READONLY_AX_imm16(helper, name) \
        void VCpu::name() { \
                helper ## 16(this, getAX(), fetchOpcodeWord()); \
	}

#define READONLY_EAX_imm32(helper, name) \
        void VCpu::name() { \
                helper ## 32(this, getEAX(), fetchOpcodeDWord()); \
        }

#define DEFAULT_RM32_reg32(helper, name) \
        void VCpu::name() { \
                BYTE rm = fetchOpcodeByte(); \
                DWORD value = readModRM32(rm); \
                updateModRM32(helper ## 32(this, value, *treg32[vomit_modRMRegisterPart(rm)])); \
        }

#endif /* __templates_h__ */
