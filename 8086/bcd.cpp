/* 8086/bcd.cpp
 * BCD instruction handlers
 *
 * Based entirely on the Intel IA32 manual.
 *
 */

#include "vomit.h"

void _AAA(vomit_cpu_t *cpu)
{
    if (((cpu->regs.B.AL & 0x0F)>9) || cpu->getAF()) {
        cpu->regs.B.AL += 6;
        cpu->regs.B.AH += 1;
        cpu->setAF(1);
        cpu->setCF(1);
    } else {
        cpu->setAF(0);
        cpu->setCF(0);
    }
    cpu->regs.B.AL &= 0x0F;
}

void _AAM(vomit_cpu_t *cpu)
{
    BYTE imm = vomit_cpu_pfq_getbyte(cpu);

    if (imm == 0) {
        /* Exceptions return to offending IP. */
        --cpu->IP;
        vomit_cpu_isr_call(cpu, 0);
        return;
    }

    BYTE tempAL = cpu->regs.B.AL;
    cpu->regs.B.AH = tempAL / imm;
    cpu->regs.B.AL = tempAL % imm;
    cpu->updateFlags8(cpu->regs.B.AL);
}

void _AAD(vomit_cpu_t *cpu)
{
    BYTE tempAL = cpu->regs.B.AL;
    BYTE tempAH = cpu->regs.B.AH;
    BYTE imm = vomit_cpu_pfq_getbyte(cpu);

    cpu->regs.B.AL = (tempAL + (tempAH * imm)) & 0xFF;
    cpu->regs.B.AH = 0x00;
    cpu->updateFlags8(cpu->regs.B.AL);
}

void _AAS(vomit_cpu_t *cpu)
{
    if (((cpu->regs.B.AL & 0x0F) > 9) || cpu->getAF()) {
        cpu->regs.B.AL -= 6;
        cpu->regs.B.AH -= 1;
        cpu->setAF(1);
        cpu->setCF(1);
    } else {
        cpu->setAF(0);
        cpu->setCF(0);
    }
}

void _DAS(vomit_cpu_t *cpu)
{
    bool oldCF = cpu->getCF();
    BYTE oldAL = cpu->regs.B.AL;

    cpu->setCF(0);

    if (((cpu->regs.B.AL & 0x0F) > 0x09) || cpu->getAF()) {
        cpu->setCF(((cpu->regs.B.AL - 6) >> 8) & 1);
        cpu->regs.B.AL -= 0x06;
        cpu->setCF(oldCF | cpu->getCF());
        cpu->setAF(1);
    } else {
        cpu->setAF(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        cpu->regs.B.AL -= 0x60;
        cpu->setCF(1);
    } else {
        cpu->setCF(0);
    }
}

void _DAA(vomit_cpu_t *cpu)
{
    bool oldCF = cpu->getCF();
    BYTE oldAL = cpu->regs.B.AL;

    cpu->setCF(0);

    if (((cpu->regs.B.AL & 0x0F) > 0x09) || cpu->getAF()) {
        cpu->setCF(((cpu->regs.B.AL + 6) >> 8) & 1);
        cpu->regs.B.AL += 6;
        cpu->setCF(oldCF | cpu->getCF());
        cpu->setAF(1);
    } else {
        cpu->setAF(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        cpu->regs.B.AL += 0x60;
        cpu->setCF(1);
    } else {
        cpu->setCF(0);
    }
}
