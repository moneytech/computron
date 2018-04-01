// Computron x86 PC Emulator
// Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "CPU.h"
#include "machine.h"
#include "Common.h"
#include "debug.h"
#include "debugger.h"
#include "pic.h"
#include "settings.h"
#include <unistd.h>
#include "pit.h"
#include "Tasking.h"

#define CRASH_ON_OPCODE_00_00
#define CRASH_ON_VM
#define A20_ENABLED
//#define LOG_FAR_JUMPS
//#define MEMORY_DEBUGGING
//#define DEBUG_WARCRAFT2
//#define DEBUG_BOUND

#ifdef MEMORY_DEBUGGING
static bool shouldLogAllMemoryAccesses(PhysicalAddress address)
{
    UNUSED_PARAM(address);
#ifdef CT_DETERMINISTIC
    return true;
#endif
    return false;
}

static bool shouldLogMemoryPointer(PhysicalAddress address)
{
    if (shouldLogAllMemoryAccesses(address))
        return true;
    return false;
}

static bool shouldLogMemoryWrite(PhysicalAddress address)
{
    if (shouldLogAllMemoryAccesses(address))
        return true;
    return false;
}

static bool shouldLogMemoryRead(PhysicalAddress address)
{
    if (shouldLogAllMemoryAccesses(address))
        return true;
    return false;
}
#endif

CPU* g_cpu = 0;

template<typename T>
T CPU::readRegister(int registerIndex)
{
    if (sizeof(T) == 1)
        return *treg8[registerIndex];
    if (sizeof(T) == 2)
        return *treg16[registerIndex];
    if (sizeof(T) == 4)
        return *treg32[registerIndex];
    ASSERT_NOT_REACHED();
}

DWORD CPU::readRegisterForAddressSize(int registerIndex)
{
    if (a32())
        return *treg32[registerIndex];
    return *treg16[registerIndex];
}

void CPU::writeRegisterForAddressSize(int registerIndex, DWORD data)
{
    if (a32())
        *treg32[registerIndex] = data;
    else
        *treg16[registerIndex] = data;
}

void CPU::stepRegisterForAddressSize(int registerIndex, DWORD stepSize)
{
    if (a32())
        *treg32[registerIndex] += getDF() ? -stepSize : stepSize;
    else
        *treg16[registerIndex] += getDF() ? -stepSize : stepSize;
}

bool CPU::decrementCXForAddressSize()
{
    if (a32()) {
        setECX(getECX() - 1);
        return getECX() == 0;
    }
    setCX(getCX() - 1);
    return getCX() == 0;
}

template<typename T>
void CPU::writeRegister(int registerIndex, T value)
{
    if (sizeof(T) == 1)
        *treg8[registerIndex] = value;
    else if (sizeof(T) == 2)
        *treg16[registerIndex] = value;
    else if (sizeof(T) == 4)
        *treg32[registerIndex] = value;
    else
        ASSERT_NOT_REACHED();
}

template BYTE CPU::readRegister<BYTE>(int);
template WORD CPU::readRegister<WORD>(int);
template DWORD CPU::readRegister<DWORD>(int);
template void CPU::writeRegister<BYTE>(int, BYTE);
template void CPU::writeRegister<WORD>(int, WORD);
template void CPU::writeRegister<DWORD>(int, DWORD);

FLATTEN void CPU::decodeNext()
{
#ifdef CT_TRACE
    if (UNLIKELY(m_isForAutotest))
        dumpTrace();
#endif

    auto insn = Instruction::fromStream(*this, m_operandSize32, m_addressSize32);
    if (!insn.isValid())
        throw InvalidOpcode();
    execute(insn);
}

FLATTEN void CPU::execute(Instruction& insn)
{
#ifdef CRASH_ON_VM
    if (UNLIKELY(getVM())) {
        dumpTrace();
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef CRASH_ON_OPCODE_00_00
    if (UNLIKELY(insn.op() == 0 && insn.rm() == 0)) {
        dumpTrace();
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef DISASSEMBLE_EVERYTHING
    if (options.disassembleEverything)
        vlog(LogCPU, "%s", qPrintable(insn.toString(m_baseEIP, x32())));
#endif
    insn.execute(*this);

    ++m_cycle;
}

void CPU::_RDTSC(Instruction&)
{
    if (getPE() && getCPL() != 0) {
        throw GeneralProtectionFault(0, "RDTSC with CPL != 0");
    }
    setEDX(m_cycle >> 32);
    setEAX(m_cycle);
}

void CPU::_WBINVD(Instruction&)
{
    if (getPE() && getCPL() != 0) {
        throw GeneralProtectionFault(0, "WBINVD");
    }
}

void CPU::_VKILL(Instruction&)
{
    // FIXME: Maybe (0xf1) is a bad choice of opcode here, since that's also INT1 / ICEBP.
    if (!machine().isForAutotest()) {
        throw InvalidOpcode("VKILL (0xf1) is an invalid opcode outside of auto-test mode!");
    }
    vlog(LogCPU, "0xF1: Secret shutdown command received!");
    //dumpAll();
    hard_exit(0);
}

void CPU::setMemorySizeAndReallocateIfNeeded(DWORD size)
{
    if (m_memorySize == size)
        return;
    delete [] m_memory;
    m_memorySize = size;
    m_memory = new BYTE[m_memorySize];
    if (!m_memory) {
        vlog(LogInit, "Insufficient memory available.");
        hard_exit(1);
    }
    memset(m_memory, 0x0, m_memorySize);
}

CPU::CPU(Machine& m)
    : m_machine(m)
    , m_shouldBreakOutOfMainLoop(false)
{
    m_isForAutotest = machine().isForAutotest();

    buildOpcodeTablesIfNeeded();

    ASSERT(!g_cpu);
    g_cpu = this;

    setMemorySizeAndReallocateIfNeeded(8192 * 1024);

    memset(m_memoryProviders, 0, sizeof(m_memoryProviders));

    m_debugger = make<Debugger>(*this);

    m_controlRegisterMap[0] = &m_CR0;
    m_controlRegisterMap[1] = &m_CR1;
    m_controlRegisterMap[2] = &m_CR2;
    m_controlRegisterMap[3] = &m_CR3;
    m_controlRegisterMap[4] = &m_CR4;
    m_controlRegisterMap[5] = &m_CR5;
    m_controlRegisterMap[6] = &m_CR6;
    m_controlRegisterMap[7] = &m_CR7;

    m_debugRegisterMap[0] = &m_DR0;
    m_debugRegisterMap[1] = &m_DR1;
    m_debugRegisterMap[2] = &m_DR2;
    m_debugRegisterMap[3] = &m_DR3;
    m_debugRegisterMap[4] = &m_DR4;
    m_debugRegisterMap[5] = &m_DR5;
    m_debugRegisterMap[6] = &m_DR6;
    m_debugRegisterMap[7] = &m_DR7;

    this->treg32[RegisterEAX] = &this->regs.D.EAX;
    this->treg32[RegisterEBX] = &this->regs.D.EBX;
    this->treg32[RegisterECX] = &this->regs.D.ECX;
    this->treg32[RegisterEDX] = &this->regs.D.EDX;
    this->treg32[RegisterESP] = &this->regs.D.ESP;
    this->treg32[RegisterEBP] = &this->regs.D.EBP;
    this->treg32[RegisterESI] = &this->regs.D.ESI;
    this->treg32[RegisterEDI] = &this->regs.D.EDI;

    this->treg16[RegisterAX] = &this->regs.W.AX;
    this->treg16[RegisterBX] = &this->regs.W.BX;
    this->treg16[RegisterCX] = &this->regs.W.CX;
    this->treg16[RegisterDX] = &this->regs.W.DX;
    this->treg16[RegisterSP] = &this->regs.W.SP;
    this->treg16[RegisterBP] = &this->regs.W.BP;
    this->treg16[RegisterSI] = &this->regs.W.SI;
    this->treg16[RegisterDI] = &this->regs.W.DI;

    this->treg8[RegisterAH] = &this->regs.B.AH;
    this->treg8[RegisterBH] = &this->regs.B.BH;
    this->treg8[RegisterCH] = &this->regs.B.CH;
    this->treg8[RegisterDH] = &this->regs.B.DH;
    this->treg8[RegisterAL] = &this->regs.B.AL;
    this->treg8[RegisterBL] = &this->regs.B.BL;
    this->treg8[RegisterCL] = &this->regs.B.CL;
    this->treg8[RegisterDL] = &this->regs.B.DL;

    m_segmentMap[(int)SegmentRegisterIndex::CS] = &this->CS;
    m_segmentMap[(int)SegmentRegisterIndex::DS] = &this->DS;
    m_segmentMap[(int)SegmentRegisterIndex::ES] = &this->ES;
    m_segmentMap[(int)SegmentRegisterIndex::SS] = &this->SS;
    m_segmentMap[(int)SegmentRegisterIndex::FS] = &this->FS;
    m_segmentMap[(int)SegmentRegisterIndex::GS] = &this->GS;
    m_segmentMap[6] = nullptr;
    m_segmentMap[7] = nullptr;

    reset();
}

void CPU::reset()
{
    m_a20Enabled = false;
    m_nextInstructionIsUninterruptible = false;

    memset(&regs, 0, sizeof(regs));
    m_CR0 = 0;
    m_CR1 = 0;
    m_CR2 = 0;
    m_CR3 = 0;
    m_CR4 = 0;
    m_CR5 = 0;
    m_CR6 = 0;
    m_CR7 = 0;
    m_DR0 = 0;
    m_DR1 = 0;
    m_DR2 = 0;
    m_DR3 = 0;
    m_DR4 = 0;
    m_DR5 = 0;
    m_DR6 = 0;
    m_DR7 = 0;

    this->IOPL = 0;
    this->VM = 0;
    this->VIP = 0;
    this->VIF = 0;
    this->NT = 0;
    this->RF = 0;
    this->AC = 0;
    this->ID = 0;

    this->GDTR.base = 0;
    this->GDTR.limit = 0;
    this->IDTR.base = 0;
    this->IDTR.limit = 0;
    this->LDTR.base = 0;
    this->LDTR.limit = 0;
    this->LDTR.segment = 0;
    this->TR.segment = 0;
    this->TR.limit = 0;
    this->TR.base = 0;
    this->TR.is32Bit = false;

    memset(m_descriptor, 0, sizeof(m_descriptor));

    m_segmentPrefix = SegmentRegisterIndex::None;

    setCS(0);
    setDS(0);
    setES(0);
    setSS(0);
    setFS(0);
    setGS(0);

    if (m_isForAutotest)
        jump32(machine().settings().entryCS(), machine().settings().entryIP(), JumpType::Internal);
    else
        jump32(0xF000, 0x00000000, JumpType::Internal);

    setFlags(0x0200);

    setIOPL(3);

    m_state = Alive;

    m_addressSize32 = false;
    m_operandSize32 = false;
    m_effectiveAddressSize32 = false;
    m_effectiveOperandSize32 = false;

    m_dirtyFlags = 0;
    m_lastResult = 0;
    m_lastOpSize = ByteSize;

    m_cycle = 0;

    initWatches();

    recomputeMainLoopNeedsSlowStuff();
}

CPU::~CPU()
{
    delete [] m_memory;
    m_memory = nullptr;
}

class InstructionExecutionContext {
public:
    InstructionExecutionContext(CPU& cpu) : m_cpu(cpu) { cpu.saveBaseAddress(); }
    ~InstructionExecutionContext() { m_cpu.clearPrefix(); }
private:
    CPU& m_cpu;
};

FLATTEN void CPU::executeOneInstruction()
{
    try {
        InstructionExecutionContext context(*this);
        decodeNext();
    } catch(Exception e) {
        dumpDisassembled(cachedDescriptor(SegmentRegisterIndex::CS), m_baseEIP, 3);
        raiseException(e);
    } catch(HardwareInterruptDuringREP) {
        setEIP(getBaseEIP());
    }
}

void CPU::haltedLoop()
{
    while (state() == CPU::Halted) {
#ifdef HAVE_USLEEP
        usleep(100);
#endif
        if (m_shouldHardReboot) {
            hardReboot();
            return;
        }
        if (PIC::hasPendingIRQ() && getIF())
            PIC::serviceIRQ(*this);
    }
}

void CPU::queueCommand(Command command)
{
    switch (command) {
    case ExitMainLoop:
        m_shouldBreakOutOfMainLoop = true;
        break;
    case EnterMainLoop:
        m_shouldBreakOutOfMainLoop = false;
        break;
    case HardReboot:
        m_shouldHardReboot = true;
        break;
    }
    recomputeMainLoopNeedsSlowStuff();
}

void CPU::hardReboot()
{
    machine().resetAllIODevices();
    reset();
    m_shouldHardReboot = false;
}

void CPU::makeNextInstructionUninterruptible()
{
    m_nextInstructionIsUninterruptible = true;
}

void CPU::recomputeMainLoopNeedsSlowStuff()
{
    m_mainLoopNeedsSlowStuff = m_shouldBreakOutOfMainLoop ||
                               m_shouldHardReboot ||
                               options.trace ||
                               !m_breakpoints.empty() ||
                               debugger().isActive() ||
                               !m_watches.isEmpty();
}

NEVER_INLINE bool CPU::mainLoopSlowStuff()
{
    if (m_shouldBreakOutOfMainLoop)
        return false;

    if (m_shouldHardReboot) {
        hardReboot();
        return true;
    }

    if (!m_breakpoints.empty()) {
        // FIXME: This is totally wrong for protected mode.
        auto flatPC = realModeAddressToPhysicalAddress(getCS(), getEIP());
        for (auto& breakpoint : m_breakpoints) {
            if (flatPC.get() == breakpoint) {
                debugger().enter();
                break;
            }
        }
    }

    if (debugger().isActive()) {
        saveBaseAddress();
        debugger().doConsole();
    }

    if (options.trace)
        dumpTrace();

    if (!m_watches.isEmpty())
        dumpWatches();

    return true;
}

FLATTEN void CPU::mainLoop()
{
    forever {
        if (UNLIKELY(m_mainLoopNeedsSlowStuff)) {
            mainLoopSlowStuff();
        }

        executeOneInstruction();

        // FIXME: An obvious optimization here would be to dispatch next insn directly from whoever put us in this state.
        // Easy to implement: just call executeOneInstruction() in e.g "POP SS"
        // I'll do this once things feel more trustworthy in general.
        if (UNLIKELY(m_nextInstructionIsUninterruptible)) {
            m_nextInstructionIsUninterruptible = false;
            continue;
        }

        if (UNLIKELY(getTF())) {
            // The Trap Flag is set, so we'll execute one instruction and
            // call ISR 1 as soon as it's finished.
            //
            // This is used by tools like DEBUG to implement step-by-step
            // execution :-)
            jumpToInterruptHandler(1, InterruptSource::Internal);
        }

        if (PIC::hasPendingIRQ() && getIF())
            PIC::serviceIRQ(*this);

#ifdef CT_DETERMINISTIC
        if (getIF() && ((cycle() + 1) % 100 == 0)) {
            machine().pit().raiseIRQ();
        }
#endif
    }
}

void CPU::jumpRelative8(SIGNED_BYTE displacement)
{
    EIP += displacement;
}

void CPU::jumpRelative16(SIGNED_WORD displacement)
{
    EIP += displacement;
}

void CPU::jumpRelative32(SIGNED_DWORD displacement)
{
    EIP += displacement;
}

void CPU::jumpAbsolute16(WORD address)
{
    EIP = address;
}

void CPU::jumpAbsolute32(DWORD address)
{
//    vlog(LogCPU, "[PE=%u] Abs jump to %08X", getPE(), address);
    EIP = address;
}

static const char* toString(JumpType type)
{
    switch (type) {
    case JumpType::CALL: return "CALL";
    case JumpType::RETF: return "RETF";
    case JumpType::IRET: return "IRET";
    case JumpType::INT: return "INT";
    case JumpType::JMP: return "JMP";
    case JumpType::Internal: return "Internal";
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

void CPU::jump32(WORD selector, DWORD offset, JumpType type, BYTE isr, DWORD flags, Gate* gate, std::optional<WORD> errorCode)
{
    bool pushSize16 = !getPE() || o16();

    if (getPE() && gate) {
        // Coming through a gate; respect bit size of gate descriptor!
        pushSize16 = !gate->is32Bit();
    }

    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCPL = getCPL();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();

    BYTE selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", getPE(), getPG(), toString(type), getBaseCS(), getBaseEIP(), selector, offset);
#endif

    auto descriptor = getDescriptor(selector, SegmentRegisterIndex::CS);

    if (getPE() && descriptor.isNull()) {
        throw GeneralProtectionFault(selector, QString("%1 to null selector").arg(toString(type)));
    }

    if (descriptor.isSystemDescriptor()) {
        if (gate) {
            dumpDescriptor(*gate);
            dumpDescriptor(descriptor);
            throw GeneralProtectionFault(selector, "Gate-to-gate jumps are not allowed");
        }

        auto& sys = descriptor.asSystemDescriptor();

#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s to %04x:%08x hit system descriptor type %s (%x)", toString(type), selector, offset, sys.typeName(), (unsigned)sys.type());
        dumpDescriptor(descriptor);
#endif
        if (sys.isGate()) {
            auto& gate = sys.asGate();

#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Gate (%s) to %04x:%08x (count=%u)", gate.typeName(), gate.selector(), gate.offset(), gate.parameterCount());
#endif
            ASSERT(gate.isCallGate());
            ASSERT(!gate.parameterCount()); // FIXME: Implement

            if (gate.DPL() < getCPL()) {
                throw GeneralProtectionFault(selector, QString("%1 to gate with DPL(%2) < CPL(%3)").arg(toString(type)).arg(gate.DPL()).arg(getCPL()));
            }

            if (gate.DPL() < selectorRPL) {
                throw GeneralProtectionFault(selector, QString("%1 to gate with DPL(%2) < RPL(%3)").arg(toString(type)).arg(gate.DPL()).arg(selectorRPL));
            }

            if (!gate.present()) {
                throw NotPresent(selector, QString("Gate not present"));
            }

            // NOTE: We recurse here, jumping to the gate entry point.
            jump32(gate.selector(), gate.offset(), type, isr, flags, &gate);
            return;
        } else if (sys.isTSS()) {
            auto& tssDescriptor = sys.asTSSDescriptor();
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "CS is this:");
            dumpDescriptor(cachedDescriptor(SegmentRegisterIndex::CS));
            vlog(LogCPU, "%s to TSS descriptor (%s) -> %08x", toString(type), tssDescriptor.typeName(), tssDescriptor.base());
#endif
            taskSwitch(tssDescriptor, type);
        } else {
            vlog(LogCPU, "%s to %04x:%08x hit unhandled descriptor type %s (%x)", toString(type), selector, offset, sys.typeName(), (unsigned)sys.type());
            dumpDescriptor(descriptor);
            ASSERT_NOT_REACHED();
        }
    } else { // it's a segment descriptor
        if (!descriptor.isCode()) {
            dumpDescriptor(descriptor);
            ASSERT(getPE());
            throw GeneralProtectionFault(selector, "Not a code segment");
        }
        auto& codeSegment = descriptor.asCodeSegmentDescriptor();

        if (getPE()) {
            if ((type == JumpType::CALL || type == JumpType::JMP) && !gate) {
                if (codeSegment.conforming()) {
                    if (codeSegment.DPL() > getCPL()) {
                        throw GeneralProtectionFault(selector, QString("%1 -> Code segment DPL(%2) > CPL(%3)").arg(toString(type)).arg(codeSegment.DPL()).arg(getCPL()));
                    }
                } else {
                    if (codeSegment.DPL() > selectorRPL) {
                        throw GeneralProtectionFault(selector, QString("%1 -> Code segment DPL(%2) > RPL(%3)").arg(toString(type)).arg(codeSegment.DPL()).arg(selectorRPL));
                    }
                    if (codeSegment.DPL() != getCPL()) {
                        throw GeneralProtectionFault(selector, QString("%1 -> Code segment DPL(%2) != CPL(%3)").arg(toString(type)).arg(codeSegment.DPL()).arg(getCPL()));
                    }
                }
            }

            if (gate && !gate->is32Bit()) {
                offset &= 0xffff;
            }

            // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
            // Mask them off to make sure we don't incorrectly fail limit checks.
            if (!codeSegment.is32Bit()) {
                offset &= 0xffff;
            }

            if (!codeSegment.present()) {
                throw NotPresent(selector, QString("Code segment not present"));
            }

            if (offset > codeSegment.effectiveLimit()) {
                vlog(LogCPU, "%s to eip(%08x) outside limit(%08x)", toString(type), offset, codeSegment.effectiveLimit());
                dumpDescriptor(codeSegment);
                throw GeneralProtectionFault(0, "Offset outside segment limit");
            }
        }
        setCS(selector);
        setEIP(offset);
        if (getPE() && (type == JumpType::CALL || type == JumpType::JMP) && !gate) {
            setCPL(originalCPL);
        }
    }

    if (getPE() && (type == JumpType::CALL || type == JumpType::INT) && gate) {
    if (descriptor.DPL() < originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s escalating privilege from ring%u to ring%u", toString(type), originalCPL, descriptor.DPL(), descriptor);
#endif
        auto tss = currentTSS();

        WORD newSS = tss.getRingSS(descriptor.DPL());
        DWORD newESP = tss.getRingESP(descriptor.DPL());
        auto newSSDescriptor = getDescriptor(newSS, SegmentRegisterIndex::SS);

        // FIXME: For JumpType::INT, exceptions related to newSS should contain the extra error code.

        if (newSSDescriptor.isNull()) {
            throw InvalidTSS(newSS, "New ss is null");
        }

        if (newSSDescriptor.isError()) {
            throw InvalidTSS(newSS, "New ss outside table limits");
        }

        if (newSSDescriptor.DPL() != descriptor.DPL()) {
            throw InvalidTSS(newSS, QString("New ss DPL(%1) != code segment DPL(%2)").arg(newSSDescriptor.DPL()).arg(descriptor.DPL()));
        }

        if (!newSSDescriptor.isData() || !newSSDescriptor.asDataSegmentDescriptor().writable()) {
            throw InvalidTSS(newSS, "New ss not a writable data segment");
        }

        if (!newSSDescriptor.present()) {
            throw StackFault(newSS, "New ss not present");
        }

        setSS(newSS);
        setESP(newESP);
        if (pushSize16) {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "%s to inner ring, ss:sp %04x:%04x -> %04x:%04x", toString(type), originalSS, originalESP, getSS(), getSP());
            vlog(LogCPU, "Push 16-bit ss:sp %04x:%04x @stack{%04x:%08x}", originalSS, originalESP, getSS(), getESP());
#endif
            push16(originalSS);
            push16(originalESP);
        } else {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "%s to inner ring, ss:esp %04x:%08x -> %04x:%08x", toString(type), originalSS, originalESP, getSS(), getESP());
            vlog(LogCPU, "Push 32-bit ss:esp %04x:%08x @stack{%04x:%08x}", originalSS, originalESP, getSS(), getESP());
#endif
            push32(originalSS);
            push32(originalESP);
        }
        setCPL(descriptor.DPL());
    } else {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s same privilege from ring%u to ring%u", toString(type), originalCPL, descriptor.DPL());
#endif
        setCPL(originalCPL);
    }
    }

    if (type == JumpType::INT) {
        if (pushSize16) {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Push 16-bit flags %04x @stack{%04x:%08x}", flags, getSS(), getESP());
#endif
            push16(flags);
        } else {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Push 32-bit flags %08x @stack{%04x:%08x}", flags, getSS(), getESP());
#endif
            push32(flags);
        }
    }

    if (type == JumpType::CALL || type == JumpType::INT) {
        if (pushSize16) {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Push 16-bit cs:ip %04x:%04x @stack{%04x:%08x}", originalCS, originalEIP, getSS(), getESP());
#endif
            push16(originalCS);
            push16(originalEIP);
        } else {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Push 32-bit cs:eip %04x:%08x @stack{%04x:%08x}", originalCS, originalEIP, getSS(), getESP());
#endif
            push32(originalCS);
            push32(originalEIP);
        }
    }

    if (errorCode.has_value()) {
        if (pushSize16)
            push16(errorCode.value());
        else
            push32(errorCode.value());
    }
}

void CPU::protectedFarReturn(WORD selector, DWORD offset, JumpType type)
{
    ASSERT(getPE());
    ASSERT(type == JumpType::RETF || type == JumpType::IRET);
#ifdef DEBUG_JUMPS
    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();
#endif

    WORD originalCPL = getCPL();
    BYTE selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", getPE(), getPG(), toString(type), getBaseCS(), getBaseEIP(), selector, offset);
#endif

    auto descriptor = getDescriptor(selector, SegmentRegisterIndex::CS);

    if (!(selectorRPL >= getCPL())) {
        throw GeneralProtectionFault(selector, QString("%1 with !(RPL(%2) >= CPL(%3))").arg(toString(type)).arg(selectorRPL).arg(getCPL()));
    }
    if (descriptor.isNull()) {
        throw GeneralProtectionFault(selector, QString("%1 to null selector").arg(toString(type)));
    }

    if (descriptor.isSystemDescriptor()) {
        ASSERT_NOT_REACHED();
        throw GeneralProtectionFault(selector, QString("%1 to system descriptor!?").arg(toString(type)));
    }

    if (!descriptor.isCode()) {
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(selector, "Not a code segment");
    }

    auto& codeSegment = descriptor.asCodeSegmentDescriptor();

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is32Bit()) {
        offset &= 0xffff;
    }

    if (!codeSegment.present()) {
        throw NotPresent(selector, QString("Code segment not present"));
    }

    if (offset > codeSegment.effectiveLimit()) {
        vlog(LogCPU, "%s to eip(%08x) outside limit(%08x)", toString(type), offset, codeSegment.effectiveLimit());
        dumpDescriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    setCS(selector);
    setEIP(offset);

    if (selectorRPL > originalCPL) {
        if (o16()) {
            WORD newSP = pop16();
            WORD newSS = pop16();
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Popped 16-bit ss:sp %04x:%04x @stack{%04x:%08x}", newSS, newSP, getSS(), getESP());
            vlog(LogCPU, "%s from ring%u to ring%u, ss:sp %04x:%04x -> %04x:%04x", toString(type), originalCPL, getCPL(), originalSS, originalESP, newSS, newSP);
#endif
            setESP(newSP);
            setSS(newSS);
        } else {
            DWORD newESP = pop32();
            WORD newSS = pop32();
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Popped 32-bit ss:esp %04x:%08x @stack{%04x:%08x}", newSS, newESP, getSS(), getESP());
            vlog(LogCPU, "%s from ring%u to ring%u, ss:esp %04x:%08x -> %04x:%08x", toString(type), originalCPL, getCPL(), originalSS, originalESP, newSS, newESP);
#endif
            setESP(newESP);
            setSS(newSS);
        }

        auto clearSegmentRegisterIfNeeded = [this, type] (SegmentRegisterIndex segreg) {
            if (readSegmentRegister(segreg) == 0)
                return;
            auto& cached = cachedDescriptor(segreg);
            if (cached.isNull() || (cached.DPL() < getCPL() && (cached.isData() || cached.isNonconformingCode()))) {
                vlog(LogCPU, "%s clearing %s(%04x) with DPL=%u (CPL now %u)", toString(type), registerName(segreg), readSegmentRegister(segreg), cached.DPL(), getCPL());
                writeSegmentRegister(segreg, 0);
            }
        };

        clearSegmentRegisterIfNeeded(SegmentRegisterIndex::ES);
        clearSegmentRegisterIfNeeded(SegmentRegisterIndex::FS);
        clearSegmentRegisterIfNeeded(SegmentRegisterIndex::GS);
        clearSegmentRegisterIfNeeded(SegmentRegisterIndex::DS);
    }
}

void CPU::farReturn(JumpType type, WORD stackAdjustment)
{
    WORD selector;
    DWORD offset;
    DWORD flags;
    BYTE originalCPL = getCPL();

    if (o16()) {
        offset = pop16();
        selector = pop16();
        adjustStackPointer(stackAdjustment);
        if (type == JumpType::IRET) {
            flags = pop16();
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Popped 16-bit cs:ip!flags %04x:%04x!%04x @stack{%04x:%08x}", selector, offset, flags, getSS(), getESP());
#endif
        }
    } else {
        offset = pop32();
        selector = pop32();
        adjustStackPointer(stackAdjustment);
        if (type == JumpType::IRET) {
            flags = pop32();
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "Popped 32-bit cs:eip!flags %04x:%08x!%08x @stack{%04x:%08x}", selector, offset, flags, getSS(), getESP());
#endif
        }
    }

    if (getPE()) {
        protectedFarReturn(selector, offset, type);
        if (getCPL() != originalCPL)
            adjustStackPointer(stackAdjustment);
    } else {
        setCS(selector);
        setEIP(offset);
    }

    if (type == JumpType::IRET) {
        setEFlagsRespectfully(flags);
    }
}

void CPU::setCPL(BYTE cpl)
{
    ASSERT(getPE());
    CS = (CS & ~3) | cpl;
    cachedDescriptor(SegmentRegisterIndex::CS).m_RPL = cpl;
}

void CPU::jump16(WORD selector, WORD offset, JumpType type, BYTE isr, DWORD flags, Gate* gate, std::optional<WORD> errorCode)
{
    jump32(selector, offset, type, isr, flags, gate, errorCode);
}

void CPU::_NOP(Instruction&)
{
}

void CPU::_HLT(Instruction&)
{
    if (getCPL() != 0) {
        throw GeneralProtectionFault(0, QString("HLT with CPL!=0(%1)").arg(getCPL()));
    }

    setState(CPU::Halted);

    if (!getIF()) {
        vlog(LogCPU, "Halted with IF=0");
    } else {
#ifdef VERBOSE_DEBUG
        vlog(LogCPU, "Halted");
#endif
    }

    haltedLoop();
}

void CPU::_XLAT(Instruction&)
{
    if (a32())
        setAL(readMemory8(currentSegment(), getEBX() + getAL()));
    else
        setAL(readMemory8(currentSegment(), getBX() + getAL()));
}

void CPU::_XCHG_AX_reg16(Instruction& insn)
{
    qSwap(insn.reg16(), regs.W.AX);
}

void CPU::_XCHG_EAX_reg32(Instruction& insn)
{
    qSwap(insn.reg32(), regs.D.EAX);
}

void CPU::_XCHG_reg8_RM8(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto tmp = insn.reg8();
    insn.reg8() = modrm.read8();
    modrm.write8(tmp);
}

void CPU::_XCHG_reg16_RM16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto tmp = insn.reg16();
    insn.reg16() = modrm.read16();
    modrm.write16(tmp);
}

void CPU::_XCHG_reg32_RM32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto tmp = insn.reg32();
    insn.reg32() = modrm.read32();
    modrm.write32(tmp);
}

void CPU::_DEC_reg16(Instruction& insn)
{
    auto& reg = insn.reg16();
    DWORD i = reg;

    /* Overflow if we'll wrap. */
    setOF(reg == 0x8000);

    --i;
    adjustFlag32(i, reg, 1);
    updateFlags16(i);
    --reg;
}

void CPU::_DEC_reg32(Instruction& insn)
{
    auto& reg = insn.reg32();
    QWORD i = reg;

    /* Overflow if we'll wrap. */
    setOF(reg == 0x80000000);

    --i;
    adjustFlag32(i, reg, 1);
    updateFlags32(i);
    --reg;
}

void CPU::_INC_reg16(Instruction& insn)
{
    auto& reg = insn.reg16();
    DWORD i = reg;

    /* Overflow if we'll wrap. */
    setOF(i == 0x7FFF);

    ++i;
    adjustFlag32(i, reg, 1);
    updateFlags16(i);
    ++reg;
}

void CPU::_INC_reg32(Instruction& insn)
{
    auto& reg = insn.reg32();
    QWORD i = reg;

    /* Overflow if we'll wrap. */
    setOF(i == 0x7FFFFFFF);

    ++i;
    adjustFlag32(i, reg, 1);
    updateFlags32(i);
    ++reg;
}

void CPU::_INC_RM16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read16();
    DWORD i = value;

    /* Overflow if we'll wrap. */
    setOF(value == 0x7FFF);

    ++i;
    adjustFlag32(i, value, 1);
    updateFlags16(i);
    modrm.write16(value + 1);
}

void CPU::_INC_RM32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read32();
    QWORD i = value;

    /* Overflow if we'll wrap. */
    setOF(value == 0x7FFFFFFF);

    ++i;
    adjustFlag32(i, value, 1);
    updateFlags32(i);
    modrm.write32(value + 1);
}

void CPU::_DEC_RM16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read16();
    DWORD i = value;

    /* Overflow if we'll wrap. */
    setOF(value == 0x8000);

    --i;
    adjustFlag32(i, value, 1); // XXX: i can be (dword)(-1)...
    updateFlags16(i);
    modrm.write16(value - 1);
}

void CPU::_DEC_RM32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read32();
    QWORD i = value;

    /* Overflow if we'll wrap. */
    setOF(value == 0x80000000);

    --i;
    adjustFlag32(i, value, 1); // XXX: i can be (dword)(-1)...
    updateFlags32(i);
    modrm.write32(value - 1);
}

void CPU::_INC_RM8(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read8();
    WORD i = value;
    setOF(value == 0x7F);
    i++;
    adjustFlag32(i, value, 1);
    updateFlags8(i);
    modrm.write8(value + 1);
}

void CPU::_DEC_RM8(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read8();
    WORD i = value;
    setOF(value == 0x80);
    i--;
    adjustFlag32(i, value, 1);
    updateFlags8(i);
    modrm.write8(value - 1);
}

template<typename T>
void CPU::doLxS(Instruction& insn, SegmentRegisterIndex segreg)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LxS with register operand");
    }
    auto offset = readMemory<T>(insn.modrm().segment(), insn.modrm().offset());
    WORD selector = readMemory16(insn.modrm().segment(), insn.modrm().offset() + sizeof(T));
    insn.reg<T>() = offset;
    writeSegmentRegister(segreg, selector);
}

void CPU::_LDS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LDS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LES_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LES_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LFS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LFS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LSS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LSS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LGS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LGS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LEA_reg32_mem32(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LEA_reg32_mem32 with register source");
    }
    insn.reg32() = insn.modrm().offset();
}

void CPU::_LEA_reg16_mem16(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LEA_reg16_mem16 with register source");
    }
    insn.reg16() = insn.modrm().offset();
}

// FIXME: Have VGA listen for writes to 0xB8000 somehow?
inline void CPU::didTouchMemory(DWORD address)
{
    bool shouldNotifyScreen = false;
    if (address >= 0xB8000 && address < 0xC0000)
        shouldNotifyScreen = true;
    if (shouldNotifyScreen)
        machine().notifyScreen();
}

static const char* toString(CPU::MemoryAccessType type)
{
    switch (type) {
    case CPU::MemoryAccessType::Read: return "Read";
    case CPU::MemoryAccessType::Write: return "Write";
    case CPU::MemoryAccessType::Execute: return "Execute";
    case CPU::MemoryAccessType::InternalPointer: return "InternalPointer";
    default: return "(wat)";
    }
}

ALWAYS_INLINE void CPU::translateAddress(DWORD linearAddress, PhysicalAddress& physicalAddress, MemoryAccessType accessType)
{
    if (!getPE() || !getPG()) {
        physicalAddress.set(linearAddress);
        return;
    }
    translateAddressSlowCase(linearAddress, physicalAddress, accessType);
}

static WORD makePFErrorCode(PageFaultFlags::Flags flags, CPU::MemoryAccessType accessType, bool inUserMode)
{
    return flags
         | (accessType == CPU::MemoryAccessType::Write ? PageFaultFlags::Write : PageFaultFlags::Read)
         | (inUserMode ? PageFaultFlags::UserMode : PageFaultFlags::SupervisorMode)
         | (accessType == CPU::MemoryAccessType::Execute ? PageFaultFlags::InstructionFetch : 0);
}

Exception CPU::PageFault(DWORD linearAddress, PageFaultFlags::Flags flags, CPU::MemoryAccessType accessType, bool inUserMode, const char* faultTable, DWORD pde, DWORD pte)
{
    WORD error = makePFErrorCode(flags, accessType, inUserMode);
    vlog(LogCPU, "Exception: #PF(%04x) %s in %s for %s %s @%08x, PDBR=%08x, PDE=%08x, PTE=%08x",
         error,
         (flags & PageFaultFlags::ProtectionViolation) ? "PV" : "NP",
         faultTable,
         inUserMode ? "User" : "Supervisor",
         toString(accessType),
         linearAddress,
         getCR3(),
         pde,
         pte
    );
    m_CR2 = linearAddress;
    if (options.crashOnPF) {
        dumpAll();
        vlog(LogAlert, "CRASH ON #PF");
        ASSERT_NOT_REACHED();
    }
#ifdef DEBUG_WARCRAFT2
    if (getEIP() == 0x100c2f7c) {
        vlog(LogAlert, "CRASH ON specific #PF");
        ASSERT_NOT_REACHED();
    }
#endif
    return Exception(0xe, error, linearAddress, "Page fault");
}

void CPU::translateAddressSlowCase(DWORD linearAddress, PhysicalAddress& physicalAddress, MemoryAccessType accessType)
{
    ASSERT(getCR3() < m_memorySize);

    DWORD dir = (linearAddress >> 22) & 0x3FF;
    DWORD page = (linearAddress >> 12) & 0x3FF;
    DWORD offset = linearAddress & 0xFFF;

    DWORD* PDBR = reinterpret_cast<DWORD*>(&m_memory[getCR3()]);
    ASSERT(!(getCR3() & 0x03ff));
    DWORD& pageDirectoryEntry = PDBR[dir];

    DWORD* pageTable = reinterpret_cast<DWORD*>(&m_memory[pageDirectoryEntry & 0xfffff000]);
    DWORD& pageTableEntry = pageTable[page];

    bool inUserMode = getCPL() == 3;

    if (!(pageDirectoryEntry & PageTableEntryFlags::Present)) {
        throw PageFault(linearAddress, PageFaultFlags::NotPresent, accessType, inUserMode, "PDE", pageDirectoryEntry);
    }

    if (!(pageTableEntry & PageTableEntryFlags::Present)) {
        throw PageFault(linearAddress, PageFaultFlags::NotPresent, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
    }

    if (inUserMode) {
        if (!(pageDirectoryEntry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PDE", pageDirectoryEntry);
        }
        if (!(pageTableEntry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
        }
    }

    if ((inUserMode || getCR0() & CR0::WP) && accessType == MemoryAccessType::Write) {
        if (!(pageDirectoryEntry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PDE", pageDirectoryEntry);
        }
        if (!(pageTableEntry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
        }
    }

    if (accessType == MemoryAccessType::Write)
        pageTableEntry |= PageTableEntryFlags::Dirty;

    pageDirectoryEntry |= PageTableEntryFlags::Accessed;
    pageTableEntry |= PageTableEntryFlags::Accessed;

    physicalAddress.set((pageTableEntry & 0xfffff000) | offset);

#ifdef DEBUG_PAGING
    vlog(LogCPU, "PG=1 Translating %08x {dir=%03x, page=%03x, offset=%03x} => %08x [%08x + %08x]", linearAddress, dir, page, offset, physicalAddress.get(), pageDirectoryEntry, pageTableEntry);
#endif
}

void CPU::snoop(DWORD linearAddress, MemoryAccessType accessType)
{
    if (!getPE())
        return;
    PhysicalAddress physicalAddress;
    translateAddress(linearAddress, physicalAddress, accessType);
}

void CPU::snoop(SegmentRegisterIndex segreg, DWORD offset, MemoryAccessType accessType)
{
    if (!getPE())
        return;
    DWORD linearAddress = cachedDescriptor(segreg).base() + offset;
    snoop(linearAddress, accessType);
}

template<typename T>
void CPU::validateAddress(const SegmentDescriptor& descriptor, DWORD offset, MemoryAccessType accessType)
{
    if (descriptor.isNull()) {
        vlog(LogAlert, "NULL! %s offset %08X into null selector (selector index: %04X)",
             toString(accessType),
             offset,
             descriptor.index());
        throw GeneralProtectionFault(0, "Access through null selector");
    }

    switch (accessType) {
    case MemoryAccessType::Read:
        if (descriptor.isCode() && !descriptor.asCodeSegmentDescriptor().readable()) {
            throw GeneralProtectionFault(0, "Attempt to read from non-readable code segment");
        }
        break;
    case MemoryAccessType::Write:
        if (!descriptor.isData()) {
            throw GeneralProtectionFault(0, "Attempt to write to non-data segment");
        }
        if (!descriptor.asDataSegmentDescriptor().writable()) {
            throw GeneralProtectionFault(0, "Attempt to write to non-writable data segment");
        }
        break;
    case MemoryAccessType::Execute:
        if (!descriptor.isCode()) {
            throw GeneralProtectionFault(0, "Attempt to execute non-code segment");
        }
        break;
    default:
        break;
    }

#if 0
    // FIXME: Is this appropriate somehow? Need to figure it out. The code below as-is breaks IRET.
    if (getCPL() > descriptor.DPL()) {
        throw GeneralProtectionFault(0, QString("Insufficient privilege for access (CPL=%1, DPL=%2)").arg(getCPL()).arg(descriptor.DPL()));
    }
#endif

    if (offset > descriptor.effectiveLimit()) {
        vlog(LogAlert, "FUG! %s offset %08X outside limit (selector index: %04X, effective limit: %08X [%08X x %s])",
             toString(accessType),
             offset,
             descriptor.index(),
             descriptor.effectiveLimit(),
             descriptor.limit(),
             descriptor.granularity() ? "4K" : "1b"
             );
        //ASSERT_NOT_REACHED();
        dumpDescriptor(descriptor);
        //dumpAll();
        //debugger().enter();
        throw GeneralProtectionFault(descriptor.index(), "Access outside segment limit");
    }
}

template<typename T>
void CPU::validateAddress(SegmentRegisterIndex registerIndex, DWORD offset, MemoryAccessType accessType)
{
    validateAddress<T>(m_descriptor[(int)registerIndex], offset, accessType);
}

template<typename T>
bool CPU::validatePhysicalAddress(PhysicalAddress physicalAddress, MemoryAccessType accessType)
{
    UNUSED_PARAM(accessType);
    if (physicalAddress.get() < m_memorySize)
        return true;
#ifdef MEMORY_DEBUGGING
    if (options.memdebug) {
        vlog(LogCPU, "OOB %zu-bit %s access @ physical %08x [A20=%s] [PG=%u]",
            sizeof(T) * 8,
            toString(accessType),
            physicalAddress.get(),
            isA20Enabled() ? "on" : "off",
            getPG()
        );
    }
#endif
    return false;
}

template<typename T>
T CPU::readPhysicalMemory(PhysicalAddress physicalAddress)
{
    if (auto* provider = memoryProviderForAddress(physicalAddress)) {
        if (auto* directReadAccessPointer = provider->pointerForDirectReadAccess()) {
            return *reinterpret_cast<const T*>(&directReadAccessPointer[physicalAddress.get() - provider->baseAddress().get()]);
        }
        return provider->read<T>(physicalAddress.get());
    }
    return *reinterpret_cast<T*>(&m_memory[physicalAddress.get()]);
}

template<typename T>
void CPU::writePhysicalMemory(PhysicalAddress physicalAddress, T data)
{
    if (auto* provider = memoryProviderForAddress(physicalAddress)) {
        provider->write<T>(physicalAddress.get(), data);
    } else {
        *reinterpret_cast<T*>(&m_memory[physicalAddress.get()]) = data;
    }
    didTouchMemory(physicalAddress.get());
}

template<typename T>
T CPU::readMemory(DWORD linearAddress)
{
    PhysicalAddress physicalAddress;
    translateAddress(linearAddress, physicalAddress, MemoryAccessType::Read);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
    if (!validatePhysicalAddress<T>(physicalAddress, MemoryAccessType::Read))
        return 0;
    T value = readPhysicalMemory<T>(physicalAddress);
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryRead(physicalAddress)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit read [A20=%s] 0x%08X, value: %08X\n", getBaseCS(), getBaseEIP(), sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
        else
            vlog(LogCPU, "%zu-bit read [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
    }
#endif
    return value;
}

template<typename T, CPU::MemoryAccessType accessType>
T CPU::readMemory(const SegmentDescriptor& descriptor, DWORD offset)
{
    DWORD linearAddress = descriptor.base() + offset;
    if (!getPE()) {
        return readMemory<T>(linearAddress);
    }
    validateAddress<T>(descriptor, offset, accessType);
    return readMemory<T>(linearAddress);
}

template<typename T>
T CPU::readMemory(SegmentRegisterIndex segment, DWORD offset)
{
    auto& descriptor = m_descriptor[(int)segment];
    if (!getPE())
        return readMemory<T>(descriptor.base() + offset);
    return readMemory<T>(descriptor, offset);
}

template BYTE CPU::readMemory<BYTE>(SegmentRegisterIndex, DWORD);
template WORD CPU::readMemory<WORD>(SegmentRegisterIndex, DWORD);
template DWORD CPU::readMemory<DWORD>(SegmentRegisterIndex, DWORD);

template void CPU::writeMemory<BYTE>(SegmentRegisterIndex, DWORD, BYTE);
template void CPU::writeMemory<WORD>(SegmentRegisterIndex, DWORD, WORD);
template void CPU::writeMemory<DWORD>(SegmentRegisterIndex, DWORD, DWORD);

BYTE CPU::readMemory8(DWORD address) { return readMemory<BYTE>(address); }
WORD CPU::readMemory16(DWORD address) { return readMemory<WORD>(address); }
DWORD CPU::readMemory32(DWORD address) { return readMemory<DWORD>(address); }
BYTE CPU::readMemory8(SegmentRegisterIndex segment, DWORD offset) { return readMemory<BYTE>(segment, offset); }
WORD CPU::readMemory16(SegmentRegisterIndex segment, DWORD offset) { return readMemory<WORD>(segment, offset); }
DWORD CPU::readMemory32(SegmentRegisterIndex segment, DWORD offset) { return readMemory<DWORD>(segment, offset); }

template<typename T>
void CPU::writeMemory(DWORD linearAddress, T value)
{
    PhysicalAddress physicalAddress;
    translateAddress(linearAddress, physicalAddress, MemoryAccessType::Write);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryWrite(physicalAddress)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit write [A20=%s] 0x%08X, value: %08X\n", getBaseCS(), getBaseEIP(), sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
        else
            vlog(LogCPU, "%zu-bit write [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
    }
#endif

    writePhysicalMemory(physicalAddress, value);
}

template<typename T>
void CPU::writeMemory(const SegmentDescriptor& descriptor, DWORD offset, T value)
{
    DWORD linearAddress = descriptor.base() + offset;
    if (!getPE()) {
        writeMemory(linearAddress, value);
        return;
    }
    validateAddress<T>(descriptor, offset, MemoryAccessType::Write);
    writeMemory(linearAddress, value);
}

template<typename T>
void CPU::writeMemory(SegmentRegisterIndex segment, DWORD offset, T value)
{
    auto& descriptor = m_descriptor[(int)segment];
    if (!getPE())
        return writeMemory<T>(descriptor.base() + offset, value);
    return writeMemory<T>(descriptor, offset, value);
}

void CPU::writeMemory8(DWORD address, BYTE value) { writeMemory(address, value); }
void CPU::writeMemory16(DWORD address, WORD value) { writeMemory(address, value); }
void CPU::writeMemory32(DWORD address, DWORD value) { writeMemory(address, value); }
void CPU::writeMemory8(SegmentRegisterIndex segment, DWORD offset, BYTE value) { writeMemory(segment, offset, value); }
void CPU::writeMemory16(SegmentRegisterIndex segment, DWORD offset, WORD value) { writeMemory(segment, offset, value); }
void CPU::writeMemory32(SegmentRegisterIndex segment, DWORD offset, DWORD value) { writeMemory(segment, offset, value); }

void CPU::updateDefaultSizes()
{
#ifdef VERBOSE_DEBUG
    bool oldO32 = m_operandSize32;
    bool oldA32 = m_addressSize32;
#endif

    auto& csDescriptor = m_descriptor[(int)SegmentRegisterIndex::CS];
    m_addressSize32 = csDescriptor.D();
    m_operandSize32 = csDescriptor.D();

#ifdef VERBOSE_DEBUG
    if (oldO32 != m_operandSize32 || oldA32 != m_addressSize32) {
        vlog(LogCPU, "updateDefaultSizes PE=%u X:%u O:%u A:%u (newCS: %04X)", getPE(), x16() ? 16 : 32, o16() ? 16 : 32, a16() ? 16 : 32, getCS());
        dumpDescriptor(csDescriptor);
    }
#endif
}

void CPU::updateStackSize()
{
#ifdef VERBOSE_DEBUG
    bool oldS32 = m_stackSize32;
#endif

    auto& ssDescriptor = m_descriptor[(int)SegmentRegisterIndex::SS];
    m_stackSize32 = ssDescriptor.D();

#ifdef VERBOSE_DEBUG
    if (oldS32 != m_stackSize32) {
        vlog(LogCPU, "updateStackSize PE=%u S:%u (newSS: %04x)", getPE(), s16() ? 16 : 32, getSS());
        dumpDescriptor(ssDescriptor);
    }
#endif
}

void CPU::updateCodeSegmentCache()
{
    // FIXME: We need some kind of fast pointer for fetching from CS:EIP.
}

void CPU::setCS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::CS, value);
}

void CPU::setDS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::DS, value);
}

void CPU::setES(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::ES, value);
}

void CPU::setSS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::SS, value);
}

void CPU::setFS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::FS, value);
}

void CPU::setGS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::GS, value);
}

BYTE* CPU::pointerToPhysicalMemory(PhysicalAddress physicalAddress)
{
    didTouchMemory(physicalAddress.get());
    if (auto* provider = memoryProviderForAddress(physicalAddress))
        return provider->memoryPointer(physicalAddress.get());
    return &m_memory[physicalAddress.get()];
}

BYTE* CPU::memoryPointer(SegmentRegisterIndex segment, DWORD offset)
{
    auto& descriptor = m_descriptor[(int)segment];
    if (!getPE())
        return memoryPointer(descriptor.base() + offset);
    return memoryPointer(descriptor, offset);
}

BYTE* CPU::memoryPointer(const SegmentDescriptor& descriptor, DWORD offset)
{
    DWORD linearAddress = descriptor.base() + offset;
    if (!getPE())
        return memoryPointer(linearAddress);

    validateAddress<BYTE>(descriptor, offset, MemoryAccessType::InternalPointer);
    PhysicalAddress physicalAddress;
    translateAddress(linearAddress, physicalAddress, MemoryAccessType::InternalPointer);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryPointer(physicalAddress))
        vlog(LogCPU, "MemoryPointer PE [A20=%s] %04X:%08X (phys: %08X)", isA20Enabled() ? "on" : "off", descriptor.index(), offset, physicalAddress);
#endif
    return pointerToPhysicalMemory(physicalAddress);
}

BYTE* CPU::memoryPointer(WORD segmentIndex, DWORD offset)
{
    return memoryPointer(getSegmentDescriptor(segmentIndex), offset);
}

BYTE* CPU::memoryPointer(DWORD linearAddress)
{
    PhysicalAddress physicalAddress;
    translateAddress(linearAddress, physicalAddress, MemoryAccessType::InternalPointer);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
    didTouchMemory(physicalAddress.get());
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryPointer(physicalAddress)) {
        vlog(LogCPU, "MemoryPointer PE=%u [A20=%s] linear:%08x, phys:%08x",
             getPE(),
             isA20Enabled() ? "on" : "off",
             linearAddress,
             physicalAddress.get());
    }
#endif
    return pointerToPhysicalMemory(physicalAddress);
}

BYTE CPU::readInstruction8()
{
    if (x32())
        return readMemory<BYTE, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), EIP++);
    else
        return readMemory<BYTE, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), IP++);
}

WORD CPU::readInstruction16()
{
    WORD w;
    if (x32()) {
        w = readMemory<WORD, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), getEIP());
        this->EIP += 2;
    } else {
        w = readMemory<WORD, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), getIP());
        this->IP += 2;
    }
    return w;
}

DWORD CPU::readInstruction32()
{
    DWORD d;
    if (x32()) {
        d = readMemory<DWORD, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), getEIP());
        this->EIP += 4;
    } else {
        d = readMemory<DWORD, MemoryAccessType::Execute>(cachedDescriptor(SegmentRegisterIndex::CS), getIP());
        this->IP += 4;
    }
    return d;
}

void CPU::_CPUID(Instruction&)
{
    if (getEAX() == 0) {
        setEBX(0x706d6f43);
        setEDX(0x6f727475);
        setECX(0x3638586e);
        return;
    }
}

void CPU::_LOCK(Instruction&)
{
}

void CPU::initWatches()
{
}

void CPU::registerMemoryProvider(MemoryProvider& provider)
{
    if ((provider.baseAddress().get() + provider.size()) > 1048576) {
        vlog(LogConfig, "Can't register mapper with length %u @ %08x", provider.size(), provider.baseAddress().get());
        ASSERT_NOT_REACHED();
    }

    for (unsigned i = provider.baseAddress().get() / memoryProviderBlockSize; i < (provider.baseAddress().get() + provider.size()) / memoryProviderBlockSize; ++i) {
        vlog(LogConfig, "Register memory provider %p as mapper %u", &provider, i);
        m_memoryProviders[i] = &provider;
    }
}

ALWAYS_INLINE MemoryProvider* CPU::memoryProviderForAddress(PhysicalAddress address)
{
    if (address.get() >= 1048576)
        return nullptr;
    return m_memoryProviders[address.get() / memoryProviderBlockSize];
}

void CPU::_BOUND(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("BOUND with register operand");
    }
    QString reason;
    bool isWithinBounds;
    if (o32()) {
        SIGNED_DWORD arrayIndex = insn.reg32();
        SIGNED_DWORD lowerBound = readMemory32(insn.modrm().segment(), insn.modrm().offset());
        SIGNED_DWORD upperBound = readMemory32(insn.modrm().segment(), insn.modrm().offset() + 4);
        isWithinBounds = arrayIndex >= lowerBound && arrayIndex <= upperBound;
#ifdef DEBUG_BOUND
        vlog(LogCPU, "BOUND32 checking if %d is within [%d, %d]: %s",
            arrayIndex,
            lowerBound,
            upperBound,
            isWithinBounds ? "yes" : "no");
#endif
        if (!isWithinBounds) {
            reason = QString("%1 not within [%2, %3]").arg(arrayIndex).arg(lowerBound).arg(upperBound);
        }
    } else {
        SIGNED_WORD arrayIndex = insn.reg16();
        SIGNED_WORD lowerBound = readMemory16(insn.modrm().segment(), insn.modrm().offset());
        SIGNED_WORD upperBound = readMemory16(insn.modrm().segment(), insn.modrm().offset() + 2);
        isWithinBounds = arrayIndex >= lowerBound && arrayIndex <= upperBound;
#ifdef DEBUG_BOUND
        vlog(LogCPU, "BOUND16 checking if %d is within [%d, %d]: %s",
            arrayIndex,
            lowerBound,
            upperBound,
            isWithinBounds ? "yes" : "no");
#endif
        if (!isWithinBounds) {
            reason = QString("%1 not within [%2, %3]").arg(arrayIndex).arg(lowerBound).arg(upperBound);
        }
    }
    if (!isWithinBounds) {
        throw BoundRangeExceeded(reason);
    }
}
