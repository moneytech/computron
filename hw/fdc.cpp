/*
 * NEC �PD765 FDC emulation
 * for the VOMIT 80186 emulator
 * by Andreas Kling
 */

#include "vomit.h"
#include "floppy.h"
#include "debug.h"

#define DATA_REGISTER_READY 0x80
#define DATA_FROM_FDC_TO_CPU 0x40
#define DATA_FROM_CPU_TO_FDC 0x00

static BYTE fdc_status_a(VCpu* cpu, WORD);
static BYTE fdc_status_b(VCpu* cpu, WORD);
static BYTE fdc_main_status(VCpu* cpu, WORD);
static void fdc_digital_output(VCpu* cpu, WORD, BYTE);
static void fdc_data_fifo_write(VCpu* cpu, WORD, BYTE);
static BYTE fdc_data_fifo_read(VCpu* cpu, WORD);

static BYTE current_drive;
static bool fdc_enabled;
static bool dma_io_enabled;
static bool motor[2];
static BYTE fdc_data_direction;
static BYTE fdc_current_status_register;
static BYTE fdc_status_register[4];
static BYTE fdc_command[8];
static BYTE fdc_command_size;
static BYTE fdc_command_index;
static BYTE fdc_current_drive_cylinder[2];
static BYTE fdc_current_drive_head[2];
static QList<BYTE> fdc_command_result;

static void fdc_raise_irq();

void fdc_init()
{
    vm_listen(0x3f0, fdc_status_a, 0L);
    vm_listen(0x3f1, fdc_status_b, 0L);
    vm_listen(0x3f2, 0L, fdc_digital_output);
    vm_listen(0x3f4, fdc_main_status, 0L);
    vm_listen(0x3f5, fdc_data_fifo_read, fdc_data_fifo_write);
    // etc..

    current_drive = 0;
    fdc_enabled = false;
    dma_io_enabled = false;
    fdc_data_direction = DATA_FROM_CPU_TO_FDC;
    fdc_current_status_register = 0;

    fdc_command_index = 0;
    fdc_command_size = 0;
    fdc_command[0] = 0;

    for (int i = 0; i < 2; ++i) {
        motor[i] = false;
        fdc_current_drive_cylinder[i] = 0;
        fdc_current_drive_head[i] = 0;
    }

    fdc_status_register[0] = 0;
    fdc_status_register[1] = 0;
    fdc_status_register[2] = 0;
    fdc_status_register[3] = 0;
}

BYTE fdc_status_a(VCpu*, WORD port)
{
    BYTE data = 0x00;

    if (drv_status[1] != 0) {
        /* Second drive installed */
        data |= 0x40;
    }

    vlog(VM_FDCMSG, "Reading FDC status register A, data: %02X", data);
    return data;
}

BYTE fdc_status_b(VCpu*, WORD port)
{
    vlog(VM_FDCMSG, "Reading FDC status register B");
    return 0;
}

BYTE fdc_main_status(VCpu*, WORD port)
{
    BYTE status = 0;

    // 0x80 - MRQ  - main request (1: data register ready, 0: data register not ready)
    // 0x40 - DIO  - data input/output (1: controller ? cpu, 0: cpu ? controller)
    // 0x20 - NDMA - non-DMA mode (1: controller not in DMA mode, 0: controller in DMA mode)
    // 0x10 - BUSY - device busy (1: busy, 0: ready)
    // 0x08 - ACTD ..
    // 0x04 - ACTC ..
    // 0x02 - ACTB ..
    // 0x01 - ACTA - drive X in positioning mode

    status |= 0x80; // MRQ = 1
    status |= fdc_data_direction;

    if (!dma_io_enabled)
        status |= 0x20;


    vlog(VM_FDCMSG, "Reading FDC main status register: %02X (direction: %s)", status, (fdc_data_direction == DATA_FROM_CPU_TO_FDC) ? "to FDC" : "from FDC");

    return status;
}

void fdc_digital_output(VCpu*, WORD port, BYTE data)
{
    bool old_fdc_enabled = fdc_enabled;

    vlog(VM_FDCMSG, "Writing to FDC digital output, data: %02X", data);

    current_drive = data & 3;
    fdc_enabled = (data & 0x04) != 0;
    dma_io_enabled = (data & 0x08) != 0;

    motor[0] = (data & 0x10) != 0;
    motor[1] = (data & 0x20) != 0;

    vlog(VM_FDCMSG, "  Current drive: %u", current_drive);
    vlog(VM_FDCMSG, "  FDC enabled:   %s", fdc_enabled ? "yes" : "no");
    vlog(VM_FDCMSG, "  DMA+I/O mode:  %s", dma_io_enabled ? "yes" : "no");

    vlog(VM_FDCMSG, "  Motors:        %u %u", motor[0], motor[1]);

    if (fdc_enabled != old_fdc_enabled) {
        vlog(VM_FDCMSG, "Raising IRQ");
        fdc_raise_irq();
    }
}

void fdc_execute_command()
{
    vlog(VM_FDCMSG, "Executing command %02X", fdc_command[0]);

    switch (fdc_command[0]) {
    case 0x08: // Sense Interrupt Status
        vlog(VM_FDCMSG, "Sense interrupt");
        fdc_data_direction = DATA_FROM_FDC_TO_CPU;
        fdc_command_result.clear();
        fdc_command_result.append(fdc_status_register[0]);
        fdc_command_result.append(fdc_current_drive_cylinder[0]);
        break;
    default:
        vlog(VM_FDCMSG, "Unknown command! %02X", fdc_command[0]);
    }
}

void fdc_data_fifo_write(VCpu*, WORD port, BYTE data)
{
    vlog(VM_FDCMSG, "Command: %02X", data);

    if (fdc_command_index == 0) {
        // Determine the command length
        switch (data) {
        case 0x08:
            fdc_command_size = 0;
            break;
        }
    }

    fdc_command[fdc_command_index++] = data;

    if (fdc_command_index >= fdc_command_size) {
        fdc_execute_command();
        fdc_command_index = 0;
    }
}

BYTE fdc_data_fifo_read(VCpu*, WORD port)
{
    if (fdc_command_result.isEmpty()) {
        vlog(VM_FDCMSG, "Read from empty command result register");
        return 0xAA;
    }

    BYTE value = fdc_command_result.takeFirst();
    vlog(VM_FDCMSG, "Read command result byte %02X", value);

    return value;
}

void fdc_raise_irq()
{
    fdc_status_register[0] = current_drive;
    fdc_status_register[0] |= (fdc_current_drive_head[current_drive] * 0x02);
    fdc_status_register[0] |= 0x20;
    irq(6);
}
