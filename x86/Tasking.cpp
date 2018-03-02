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

#include "CPU.h"
#include "debugger.h"
#include "Tasking.h"

#define DEBUG_TASK_SWITCH

void CPU::_STR_RM16(Instruction& insn)
{
    insn.modrm().writeClearing16(TR.segment, o32());
}

void CPU::_LTR_RM16(Instruction& insn)
{
    WORD selector = insn.modrm().read16();
    auto descriptor = getDescriptor(selector);
    ASSERT(descriptor.isGlobal()); // FIXME: Generate exception?
    ASSERT(descriptor.isTSS());
    auto& tssDescriptor = descriptor.asTSSDescriptor();
    TR.segment = selector;
    TR.base = tssDescriptor.base();
    TR.limit = tssDescriptor.limit();
    TR.is32Bit = tssDescriptor.is32Bit();
    vlog(LogAlert, "LTR { segment: %04X => base:%08X, limit:%08X }", TR.segment, TR.base, TR.limit);
}

void CPU::taskSwitch(TSSDescriptor& incomingTSSDescriptor, JumpType source)
{
    ASSERT(incomingTSSDescriptor.is32Bit());
    //ASSERT(incomingTSSDescriptor.isAvailable());

    TSSDescriptor outgoingTSSDescriptor = getDescriptor(TR.segment).asTSSDescriptor();

    TSS outgoingTSS(*this, TR.base, outgoingTSSDescriptor.is32Bit());

    outgoingTSS.setEAX(getEAX());
    outgoingTSS.setEBX(getEBX());
    outgoingTSS.setECX(getECX());
    outgoingTSS.setEDX(getEDX());
    outgoingTSS.setEBP(getEBP());
    outgoingTSS.setESP(getESP());
    outgoingTSS.setESI(getESI());
    outgoingTSS.setEDI(getEDI());

    if (source == JumpType::JMP || source == JumpType::IRET) {
        outgoingTSSDescriptor.setAvailable();
        writeToGDT(outgoingTSSDescriptor);
    }

    DWORD outgoingEFlags = getEFlags();

    if (incomingTSSDescriptor.isBusy()) {
        outgoingEFlags &= ~(1 << 14); // Clear NT flag in outgoing task.
    }

    outgoingTSS.setEFlags(outgoingEFlags);

    outgoingTSS.setCS(getCS());
    outgoingTSS.setDS(getDS());
    outgoingTSS.setES(getES());
    outgoingTSS.setFS(getFS());
    outgoingTSS.setGS(getGS());
    outgoingTSS.setSS(getSS());
    outgoingTSS.setLDT(LDTR.segment);
    outgoingTSS.setEIP(getEIP());

    if (getPG())
        outgoingTSS.setCR3(getCR3());

    TSS incomingTSS(*this, incomingTSSDescriptor.base(), incomingTSSDescriptor.is32Bit());

#ifdef DEBUG_TASK_SWITCH
    vlog(LogCPU, "Outgoing TSS:");
    dumpTSS(outgoingTSS);
    vlog(LogCPU, "Incoming TSS:");
    dumpTSS(incomingTSS);
#endif

    if (getPG())
        CR3 = incomingTSS.getCR3();

    setLDT(incomingTSS.getLDT());

    setES(incomingTSS.getES());
    setCS(incomingTSS.getCS());
    setDS(incomingTSS.getDS());
    setFS(incomingTSS.getFS());
    setGS(incomingTSS.getGS());
    setSS(incomingTSS.getSS());
    setEIP(incomingTSS.getEIP());

    DWORD incomingEFlags = incomingTSS.getEFlags();
    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingEFlags |= (1 << 14);  // Set NT in incoming task.
    }
    setEFlags(incomingEFlags);

    setEAX(incomingTSS.getEAX());
    setEBX(incomingTSS.getEBX());
    setECX(incomingTSS.getECX());
    setEDX(incomingTSS.getEDX());
    setEBP(incomingTSS.getEBP());
    setESP(incomingTSS.getESP());
    setESI(incomingTSS.getESI());
    setEDI(incomingTSS.getEDI());

    //ASSERT(incomingTSS.backlink != TR.segment);
    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingTSS.setBacklink(TR.segment);
    }

    TR.segment = incomingTSSDescriptor.index();
    TR.base = incomingTSSDescriptor.base();
    TR.limit = incomingTSSDescriptor.limit();
    TR.is32Bit = incomingTSSDescriptor.is32Bit();

    if (source != JumpType::IRET) {
        incomingTSSDescriptor.setBusy();
        writeToGDT(incomingTSSDescriptor);
    }

#if 0
    vlog(LogCPU, "Incoming:");
    dumpDescriptor(getDescriptor(incomingTSSDescriptor.index()));
    vlog(LogCPU, "Outgoing:");
    dumpDescriptor(getDescriptor(outgoingTSSDescriptor.index()));
#endif

    CR0 |= 0x04; // TS (Task Switched)

    if (getEIP() > cachedDescriptor(SegmentRegisterIndex::CS).effectiveLimit()) {
        vlog(LogCPU, "Task switch to EIP:%08x outside CS:%04x limit");
        dumpDescriptor(cachedDescriptor(SegmentRegisterIndex::CS));
        GP(0);
    }
}

void CPU::dumpTSS(const TSS &tss)
{
    vlog(LogCPU, "TSS bits=%u", tss.is32Bit() ? 32 : 16);
    vlog(LogCPU, "eax=%08x ebx=%08x ecx=%08x edx=%08x", tss.getEAX(), tss.getEBX(), tss.getECX(), tss.getEDX());
    vlog(LogCPU, "esi=%08x edi=%08x ebp=%08x esp=%08x", tss.getESI(), tss.getEDI(), tss.getEBP(), tss.getESP());
    vlog(LogCPU, "ldt=%04x backlink=%04x cr3=%08x", tss.getLDT(), tss.getBacklink(), tss.getCR3());
    vlog(LogCPU, "ds=%04x ss=%04x es=%04x fs=%04x gs=%04x", tss.getDS(), tss.getSS(), tss.getES(), tss.getFS(), tss.getGS());
    vlog(LogCPU, "cs=%04x eip=%08x eflags=%08x", tss.getCS(), tss.getEIP(), tss.getEFlags());
}

void CPU::taskSwitch(WORD task, JumpType source)
{
    // FIXME: This should mark the outgoing task as non-busy.
    auto descriptor = getDescriptor(task);
    vlog(LogCPU, "taskSwitch with selector:%04x, type:%1x", task, descriptor.type());
    auto& tssDescriptor = descriptor.asTSSDescriptor();
    taskSwitch(tssDescriptor, source);
}

TSS CPU::currentTSS()
{
    return TSS(*this, TR.base, TR.is32Bit);
}

TSS::TSS(CPU& cpu, DWORD linearAddress, bool is32Bit)
    : m_pointer(cpu.memoryPointer(linearAddress))
    , m_is32Bit(is32Bit)
{
    ASSERT(m_pointer);
}

TSS16& TSS::tss16()
{
    ASSERT(!m_is32Bit);
    return *reinterpret_cast<TSS16*>(m_pointer);
}
TSS32& TSS::tss32()
{
    ASSERT(m_is32Bit);
    return *reinterpret_cast<TSS32*>(m_pointer);
}

const TSS16& TSS::tss16() const
{
    ASSERT(!m_is32Bit);
    return *reinterpret_cast<const TSS16*>(m_pointer);
}
const TSS32& TSS::tss32() const
{
    ASSERT(m_is32Bit);
    return *reinterpret_cast<const TSS32*>(m_pointer);
}

void TSS::setBacklink(WORD value)
{
    if (m_is32Bit)
        tss32().backlink = value;
    else
        tss16().backlink = value;
}

WORD TSS::getBacklink() const
{
    if (m_is32Bit)
        return tss32().backlink;
    else
        return tss16().backlink;
}

void TSS::setLDT(WORD value)
{
    if (m_is32Bit)
        tss32().LDT = value;
    else
        tss16().LDT = value;
}

WORD TSS::getLDT() const
{
    if (m_is32Bit)
        return tss32().LDT;
    else
        return tss16().LDT;
}

DWORD TSS::getCR3() const
{
    ASSERT(m_is32Bit);
    return tss32().CR3;
}

void TSS::setCR3(DWORD value)
{
    ASSERT(m_is32Bit);
    tss32().CR3 = value;
}

DWORD TSS::getEAX() const
{
    if (m_is32Bit)
        return tss32().EAX;
    else
        return tss16().AX;
}

DWORD TSS::getEBX() const
{
    if (m_is32Bit)
        return tss32().EBX;
    else
        return tss16().BX;
}

DWORD TSS::getECX() const
{
    if (m_is32Bit)
        return tss32().ECX;
    else
        return tss16().CX;
}

DWORD TSS::getEDX() const
{
    if (m_is32Bit)
        return tss32().EDX;
    else
        return tss16().DX;
}

DWORD TSS::getESI() const
{
    if (m_is32Bit)
        return tss32().ESI;
    else
        return tss16().SI;
}

DWORD TSS::getEDI() const
{
    if (m_is32Bit)
        return tss32().EDI;
    else
        return tss16().DI;
}

DWORD TSS::getEBP() const
{
    if (m_is32Bit)
        return tss32().EBP;
    else
        return tss16().BP;
}

DWORD TSS::getESP() const
{
    if (m_is32Bit)
        return tss32().ESP;
    else
        return tss16().SP;
}

DWORD TSS::getEIP() const
{
    if (m_is32Bit)
        return tss32().EIP;
    else
        return tss16().IP;
}

DWORD TSS::getEFlags() const
{
    if (m_is32Bit)
        return tss32().EFlags;
    else
        return tss16().flags;
}

void TSS::setEAX(DWORD value)
{
    if (m_is32Bit)
        tss32().EAX = value;
    else
        tss16().AX = value;
}

void TSS::setEBX(DWORD value)
{
    if (m_is32Bit)
        tss32().EBX = value;
    else
        tss16().BX = value;
}

void TSS::setECX(DWORD value)
{
    if (m_is32Bit)
        tss32().ECX = value;
    else
        tss16().CX = value;
}

void TSS::setEDX(DWORD value)
{
    if (m_is32Bit)
        tss32().EDX = value;
    else
        tss16().DX = value;
}

void TSS::setEBP(DWORD value)
{
    if (m_is32Bit)
        tss32().EBP = value;
    else
        tss16().BP = value;
}

void TSS::setESP(DWORD value)
{
    if (m_is32Bit)
        tss32().ESP = value;
    else
        tss16().SP = value;
}

void TSS::setESI(DWORD value)
{
    if (m_is32Bit)
        tss32().ESI = value;
    else
        tss16().SI = value;
}

void TSS::setEDI(DWORD value)
{
    if (m_is32Bit)
        tss32().EDI = value;
    else
        tss16().DI = value;
}

void TSS::setEIP(DWORD value)
{
    if (m_is32Bit)
        tss32().EIP = value;
    else
        tss16().IP = value;
}

void TSS::setEFlags(DWORD value)
{
    if (m_is32Bit)
        tss32().EFlags = value;
    else
        tss16().flags = value;
}

void TSS::setCS(WORD value)
{
    if (m_is32Bit)
        tss32().CS = value;
    else
        tss16().CS = value;
}

void TSS::setDS(WORD value)
{
    if (m_is32Bit)
        tss32().DS = value;
    else
        tss16().DS = value;
}

void TSS::setES(WORD value)
{
    if (m_is32Bit)
        tss32().ES = value;
    else
        tss16().ES = value;
}

void TSS::setSS(WORD value)
{
    if (m_is32Bit)
        tss32().SS = value;
    else
        tss16().SS = value;
}

void TSS::setFS(WORD value)
{
    if (m_is32Bit)
        tss32().FS = value;
    else
        tss16().FS = value;
}

void TSS::setGS(WORD value)
{
    if (m_is32Bit)
        tss32().GS = value;
    else
        tss16().GS = value;
}

WORD TSS::getCS() const
{
    if (m_is32Bit)
        return tss32().CS;
    else
        return tss16().CS;
}

WORD TSS::getDS() const
{
    if (m_is32Bit)
        return tss32().DS;
    else
        return tss16().DS;
}

WORD TSS::getES() const
{
    if (m_is32Bit)
        return tss32().ES;
    else
        return tss16().ES;
}

WORD TSS::getSS() const
{
    if (m_is32Bit)
        return tss32().SS;
    else
        return tss16().SS;
}

WORD TSS::getFS() const
{
    if (m_is32Bit)
        return tss32().FS;
    else
        return tss16().FS;
}

WORD TSS::getGS() const
{
    if (m_is32Bit)
        return tss32().GS;
    else
        return tss16().GS;
}

DWORD TSS::getESP0() const
{
    if (m_is32Bit)
        return tss32().esp0;
    else
        return tss16().sp0;
}

DWORD TSS::getESP1() const
{
    if (m_is32Bit)
        return tss32().esp1;
    else
        return tss16().sp1;
}

DWORD TSS::getESP2() const
{
    if (m_is32Bit)
        return tss32().esp2;
    else
        return tss16().sp2;
}

WORD TSS::getSS0() const
{
    if (m_is32Bit)
        return tss32().ss0;
    else
        return tss16().ss0;
}

WORD TSS::getSS1() const
{
    if (m_is32Bit)
        return tss32().ss1;
    else
        return tss16().ss1;
}

WORD TSS::getSS2() const
{
    if (m_is32Bit)
        return tss32().ss2;
    else
        return tss16().ss2;
}

DWORD TSS::getRingESP(BYTE ring) const
{
    if (ring == 0)
        return getESP0();
    if (ring == 1)
        return getESP1();
    if (ring == 2)
        return getESP2();
    ASSERT_NOT_REACHED();
    return 0;
}

WORD TSS::getRingSS(BYTE ring) const
{
    if (ring == 0)
        return getSS0();
    if (ring == 1)
        return getSS1();
    if (ring == 2)
        return getSS2();
    ASSERT_NOT_REACHED();
    return 0;
}