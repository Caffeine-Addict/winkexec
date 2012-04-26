/* WinKexec: kexec for Windows
 * Copyright (C) 2008-2009 John Stumpo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <kexec.h>

#include "linuxboot.h"
#include "buffer.h"
#include "inlineasm.h"

/* A binary blob that we are going to need -
   namely, the boot code that will finish the job for us.  */
#include "boot/bootcode.h"
/* A structure that the binary blob uses. */
#include "boot/bootinfo.h"

/* Bail out of the boot process - all we can do now is BSoD... */
static void KEXEC_NORETURN BootPanic(PCHAR msg, ULONG_PTR code1, ULONG_PTR code2,
  ULONG_PTR code3, ULONG_PTR code4)
{
  DbgPrint("kexec: *** PANIC: %s\n", msg);
  KeBugCheckEx(0x00031337, code1, code2, code3, code4);
}

/* Stash away a pointer to a block of memory for use during boot. */
static void WriteKernelPointer(uint64_t** pd KEXEC_UNUSED,
  uint64_t** pdpos, uint64_t** pt, uint64_t** ptpos, PVOID virt_addr)
{
  /* Allocate a new page table, if necessary. */
  if ((!*ptpos) || (*ptpos - *pt >= 512)) {
    *pt = *ptpos = ExAllocatePoolWithTag(NonPagedPool,
      4096, TAG('K', 'x', 'e', 'c'));
    if (!*pt)
      BootPanic("Could not allocate page table!", 0x00000002,
        (ULONG_PTR)virt_addr, 0, 0);
    *((*pdpos)++) = MmGetPhysicalAddress(*pt).QuadPart | 0x0000000000000023ULL;
  }

  /* Write a 64-bit pointer into the page table.
     (Note: said table will be used with PAE enabled.)  */
  if (virt_addr) {
    *((*ptpos)++) = MmGetPhysicalAddress(virt_addr).QuadPart | 0x0000000000000023ULL;
  } else {
    *((*ptpos)++) = 0;
  }
}

/* By the time this is called, Windows thinks it just handed control over
   to the BIOS to reboot the computer.  As a result, nothing is really going
   on, and we can take great liberties (such as hard-coding physical memory
   addresses) in taking over and booting Linux.

   Does not return.
 */
static void KEXEC_NORETURN DoLinuxBoot(void)
{
  /* Here's how things are going to go:
     We're going to build PAE page tables that point to the
       physical addresses of 4K pages that hold the kernel, initrd, and
       kernel command line, with unmapped pages in between.  We will also
       build a PAE page directory pointing to the physical addresses of the
       page tables so that we can communicate the physical address of the
       page directory to the boot code, it can put it into its PDPT, and
       we can have nice easy access to the kernel data using virtual
       addresses before we put it back together in contiguous physical
       memory.
       Maximum length: 1 PAE page directory worth of virtual
         address space, which amounts to 1 GB.

     At physical address 0x00008000, we will copy the code that will be used
       to escape from protected mode, set things up for the boot, and
       reassemble and boot the Linux kernel.  In this file, we refer to that
       code as the "boot code."
   */
  PHYSICAL_ADDRESS addr;
  PVOID code_dest;
  ULONG i;
  uint64_t* kx_page_directory;
  uint64_t* kx_pd_position;
  uint64_t* kx_page_table;
  uint64_t* kx_pt_position;
  struct bootinfo* info_block;

  /* Allocate the page directory for the kernel map. */
  kx_page_directory = ExAllocatePoolWithTag(NonPagedPool,
    4096, TAG('K', 'x', 'e', 'c'));
  if (!kx_page_directory)
    BootPanic("Could not allocate page directory!", 0x00000001, 0, 0, 0);
  kx_pd_position = kx_page_directory;

  kx_pt_position = 0;

#define PAGE(a) WriteKernelPointer(&kx_page_directory, &kx_pd_position, \
  &kx_page_table, &kx_pt_position, (a))

  /* Write a series of pointers to pages of the kernel,
     followed by a sentinel zero.  */
  for (i = 0; i < KexecKernel.Size; i += 4096)
    PAGE(KexecKernel.Data + i);
  PAGE(0);

  /* Same for the initrd. */
  for (i = 0; i < KexecInitrd.Size; i += 4096)
    PAGE(KexecInitrd.Data + i);
  PAGE(0);

  /* And finally the kernel command line.
     (This *will* only be a single page [much less, actually!] if our new
     kernel [not to mention the boot code!] is to not complain loudly and
     fail, but treating it like the other two data chunks we already have
     will make things simpler as far as the boot code goes.)  */
  for (i = 0; i < KexecKernelCommandLine.Size; i += 4096)
    PAGE(KexecKernelCommandLine.Data + i);
  PAGE(0);

#undef PAGE

  /* Now that the paging structures are built, we must get the
     boot code into the right place and fill in the information
     table at the end of the first page of said code.  */
  addr.QuadPart = 0x0000000000008000ULL;
  code_dest = MmMapIoSpace(addr, BOOTCODE_BIN_SIZE, MmNonCached);
  RtlCopyMemory(code_dest, bootcode_bin, BOOTCODE_BIN_SIZE);
  info_block = (struct bootinfo*)(code_dest + 0x0fb0);
  info_block->kernel_size = KexecKernel.Size;
  RtlCopyMemory(info_block->kernel_hash, KexecKernel.Sha1Hash, 20);
  info_block->initrd_size = KexecInitrd.Size;
  RtlCopyMemory(info_block->initrd_hash, KexecInitrd.Sha1Hash, 20);
  info_block->cmdline_size = KexecKernelCommandLine.Size;
  RtlCopyMemory(info_block->cmdline_hash, KexecKernelCommandLine.Sha1Hash, 20);
  addr = MmGetPhysicalAddress(kx_page_directory);
  info_block->page_directory_ptr = addr.QuadPart | 0x0000000000000001ULL;
  MmUnmapIoSpace(code_dest, BOOTCODE_BIN_SIZE);

  /* Now we must prepare to execute the boot code.
     The most important preparation step is to identity-map the memory
     containing it - to make its physical and virtual addresses the same.
     We do this by direct manipulation of the page table.  This has to be
     done for it to be able to safely turn off paging, return to
     real mode, and do its thing.

     Needless to say, here be dragons!
   */

  /* Abandon all interrupts, ye who execute here! */
  cli();

  /* PAE versus non-PAE means different paging structures.
     Naturally, we will have to take that into account.
   */
  if (rcr4() & CR4_PAE) {
    /* We have PAE.
       0x00008000 = directory 0, table 0, page 8, offset 0x000
     */
    uint64_t* page_directory_pointer_table;
    uint64_t* page_directory;
    uint64_t* page_table;

    /* Where is the page directory pointer table? */
    addr.QuadPart = rcr3() & CR3_ADDR_MASK_PAE;
    page_directory_pointer_table = MmMapIoSpace(addr, 4096, MmNonCached);

    /* If the page directory isn't present, use
       the second page below the boot code.  */
    if (!(page_directory_pointer_table[0] & 0x0000000000000001ULL))
      page_directory_pointer_table[0] = 0x0000000000006000ULL;
    page_directory_pointer_table[0] |= 0x0000000000000001ULL;
    page_directory_pointer_table[0] &= 0x7fffffffffffffffULL;
    lcr3(rcr3());  /* so a modification to the PDPT takes effect */

    /* Where is the page directory? */
    addr.QuadPart = page_directory_pointer_table[0] & 0xfffffffffffff000ULL;
    page_directory = MmMapIoSpace(addr, 4096, MmNonCached);

    /* If the page table isn't present, use
       the next page below the boot code.  */
    if (!(page_directory[0] & 0x0000000000000001ULL))
      page_directory[0] = 0x0000000000007000ULL;
    page_directory[0] |= 0x0000000000000023ULL;
    page_directory[0] &= 0x7fffffffffffffffULL;

    /* Map the page table and tweak it to our needs. */
    addr.QuadPart = page_directory[0] & 0xfffffffffffff000ULL;
    page_table = MmMapIoSpace(addr, 4096, MmNonCached);
    page_table[0x08] = 0x0000000000008023ULL;
    MmUnmapIoSpace(page_table, 4096);

    MmUnmapIoSpace(page_directory, 4096);
    MmUnmapIoSpace(page_directory_pointer_table, 4096);
  } else {
    /* No PAE - it's the original x86 paging mechanism.
       0x00008000 = table 0, page 8, offset 0x000
     */
    uint32_t* page_directory;
    uint32_t* page_table;

    /* Where is the page directory? */
    addr.QuadPart = rcr3() & CR3_ADDR_MASK;
    page_directory = MmMapIoSpace(addr, 4096, MmNonCached);

    /* If the page table isn't present, use
       the next page below the boot code.  */
    if (!(page_directory[0] & 0x00000001))
      page_directory[0] = 0x00007000;
    page_directory[0] |= 0x00000023;

    /* Map the page table and tweak it to our needs. */
    addr.QuadPart = page_directory[0] & 0xfffff000;
    page_table = MmMapIoSpace(addr, 4096, MmNonCached);
    page_table[0x08] = 0x00008023;
    MmUnmapIoSpace(page_table, 4096);

    MmUnmapIoSpace(page_directory, 4096);
  }

  /* Flush the page from the TLB... */
  invlpg((void*)0x00008000);

  /* ...and away we go! */
  ((void (*)())0x00008000)();

  /* Should never happen. */
  KeBugCheckEx(0x42424242, 0x42424242, 0x42424242, 0x42424242, 0x42424242);
}

/* A kernel thread routine.
   We use this to bring down all but the first processor.
   Does not return.  */
static VOID KEXEC_NORETURN KexecThreadProc(PVOID Context KEXEC_UNUSED)
{
  HANDLE hThread;
  ULONG currentProcessor;
  KIRQL irql;

  /* Fork-bomb all but the first processor.
     To do that, we create a thread that calls this function again.  */
  PsCreateSystemThread(&hThread, GENERIC_ALL, 0, NULL,
    NULL, (PKSTART_ROUTINE)KexecThreadProc, NULL);
  ZwClose(hThread);

  /* Prevent thread switching on this processor. */
  KeRaiseIrql(DISPATCH_LEVEL, &irql);

  currentProcessor = current_processor();
  DbgPrint("KexecThreadProc() entered on processor #%d.\n", currentProcessor);

  /* If we're the first processor, go ahead. */
  if (currentProcessor == 0)
    DoLinuxBoot();

  /* Otherwise, come to a screeching halt. */
  DbgPrint("kexec: killing processor #%d", currentProcessor);
  cli_hlt();
}

/* Initiate the Linux boot process.
   Does not return.  */
void KEXEC_NORETURN KexecLinuxBoot(void)
{
  KexecThreadProc(NULL);
}
