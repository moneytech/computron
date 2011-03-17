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

#include "vomit.h"
#include "debug.h"

void VCpu::_wrap_0x0F()
{
    BYTE op = fetchOpcodeByte();
    switch (op) {
    case 0x01:
    {
        this->rmbyte = fetchOpcodeByte();
        switch (vomit_modRMRegisterPart(this->rmbyte)) {
        case 0: _SGDT(); return;
        }
        (void) readModRM16(this->rmbyte);
        vlog(VM_ALERT, "Sliding by 0F 01 /%d\n", vomit_modRMRegisterPart(this->rmbyte));
        break;
    }
    case 0xFF: // UD0
    case 0xB9: // UD1
    case 0x0B: // UD2
        vlog(VM_ALERT, "Undefined opcode 0F %02X", op);
        exception(6);
        break;
    default:
        vlog(VM_CPUMSG, "_wrap_0x0F passing opcode to VCpu::decodeNext(): 0F %02X", op);
        setEIP(getEIP() - 2);
        decodeNext();
        break;
    }
}

void VCpu::_BOUND()
{
    BYTE rm = fetchOpcodeByte();
    WORD* ptr = static_cast<WORD*>(resolveModRM8(rm));
    WORD index = getRegister16(static_cast<VCpu::RegisterIndex16>(vomit_modRMRegisterPart(rm)));

    if (index < ptr[0] || index > ptr[1]) {
        /* Raise BR exception */
        exception(5);
    }
}

void VCpu::_PUSH_imm8()
{
    push(vomit_signExtend(fetchOpcodeByte()));
}

void VCpu::_PUSH_imm16()
{
    push(fetchOpcodeWord());
}

void VCpu::_ENTER()
{
    WORD Size = fetchOpcodeWord();
    BYTE NestingLevel = fetchOpcodeByte() % 32;
    WORD FrameTemp;
    push(regs.W.BP);
    FrameTemp = regs.W.SP;
    if (NestingLevel != 0) {
        for (WORD i = 1; i <= (NestingLevel - 1); ++i) {
            regs.W.BP -= 2;
            push(readMemory16(SS, regs.W.BP));
        }
    }
    push(FrameTemp);
    regs.W.BP = FrameTemp;
    regs.W.SP = regs.W.BP - Size;
}

void VCpu::_LEAVE()
{
    regs.W.SP = regs.W.BP;
    regs.W.BP = pop();
}

void VCpu::_PUSHA()
{
    WORD oldsp = regs.W.SP;
    push(regs.W.AX);
    push(regs.W.BX);
    push(regs.W.CX);
    push(regs.W.DX);
    push(regs.W.BP);
    push(oldsp);
    push(regs.W.SI);
    push(regs.W.DI);
}

void VCpu::_PUSHAD()
{
    DWORD oldesp = regs.D.ESP;
    push32(regs.D.EAX);
    push32(regs.D.EBX);
    push32(regs.D.ECX);
    push32(regs.D.EDX);
    push32(regs.D.EBP);
    push32(oldesp);
    push32(regs.D.ESI);
    push32(regs.D.EDI);
}

void VCpu::_POPA()
{
    regs.W.DI = pop();
    regs.W.SI = pop();
    (void) pop();
    regs.W.BP = pop();
    regs.W.DX = pop();
    regs.W.CX = pop();
    regs.W.BX = pop();
    regs.W.AX = pop();
}

void VCpu::_POPAD()
{
    regs.D.EDI = pop32();
    regs.D.ESI = pop32();
    (void) pop32();
    regs.D.EBP = pop32();
    regs.D.EDX = pop32();
    regs.D.ECX = pop32();
    regs.D.EBX = pop32();
    regs.D.EAX = pop32();
}
