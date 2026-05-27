/*
 * smp.c — publish ACPI-discovered CPU topology into per-CPU storage.
 *
 * Invariants:
 *  - smp_init runs exactly once on the BSP, after acpi_init() has
 *    populated acpi_info, and before any AP bringup or LAPIC use.
 *  - cpu_count never exceeds MAX_CPUS; only smp_init writes it post-boot.
 *  - cpus[i].lapic_id reflects ACPI's MADT for every i < cpu_count.
 *  - ARCH code (acpi.c) does not mutate cpus[] directly; only this file does.
 *
 * Not allowed:
 *  - Bring up APs, send IPIs, or calibrate the LAPIC timer here
 *    (those live in smp_bringup.c, core/ipi.c, arch/x86/lapic_calibrate.c).
 *  - Run before acpi_init() or after any AP has loaded its %gs.
 */

#include <kernel/smp.h>
#include <kernel/percpu.h>
#include <kernel/acpi.h>
#include <stdint.h>

void smp_init(void) {
    if (!acpi_info.present) {
        return;
    }
    int n = acpi_info.cpu_count;
    if (n > (int)MAX_CPUS) n = (int)MAX_CPUS;
    for (int i = 0; i < n; i++) {
        cpus[i].lapic_id = acpi_info.cpu_apic_ids[i];
    }
    cpu_count = (uint32_t)n;
}
