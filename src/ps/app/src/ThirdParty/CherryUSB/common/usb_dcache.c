#include "usb_config.h"
#include "usb_dcache.h"

#ifdef CONFIG_USB_DCACHE_ENABLE

#include "xil_cache.h"

static uintptr_t PsUsbDCache_AlignDown(uintptr_t addr, uintptr_t align)
{
    return addr & ~(align - 1U);
}

static size_t PsUsbDCache_AlignSize(uintptr_t addr, size_t size, uintptr_t align)
{
    uintptr_t start;
    uintptr_t end;

    if (size == 0U) {
        return 0U;
    }

    start = PsUsbDCache_AlignDown(addr, align);
    end = (addr + size + (align - 1U)) & ~(align - 1U);
    return (size_t)(end - start);
}

void usb_dcache_clean(uintptr_t addr, size_t size)
{
    uintptr_t start;
    size_t aligned_size;

    if (size == 0U) {
        return;
    }

    start = PsUsbDCache_AlignDown(addr, 32U);
    aligned_size = PsUsbDCache_AlignSize(addr, size, 32U);
    Xil_DCacheFlushRange((INTPTR)start, (INTPTR)aligned_size);
}

void usb_dcache_invalidate(uintptr_t addr, size_t size)
{
    uintptr_t start;
    size_t aligned_size;

    if (size == 0U) {
        return;
    }

    start = PsUsbDCache_AlignDown(addr, 32U);
    aligned_size = PsUsbDCache_AlignSize(addr, size, 32U);
    Xil_DCacheInvalidateRange((INTPTR)start, (INTPTR)aligned_size);
}

void usb_dcache_flush(uintptr_t addr, size_t size)
{
    usb_dcache_clean(addr, size);
}

#endif
