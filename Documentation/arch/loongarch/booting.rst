.. SPDX-License-Identifier: GPL-2.0

=======================
Booting Linux/LoongArch
=======================

:Author: Yanteng Si <siyanteng@loongson.cn>
:Date:   18 Nov 2022

Information passed from BootLoader to kernel
============================================

LoongArch supports ACPI and FDT. The information that needs to be passed
to the kernel includes the memmap, the initrd, the command line, optionally
the ACPI/FDT tables, and so on.

The kernel is passed the following arguments on `kernel_entry` :

      - a0 = efi_boot: `efi_boot` is a flag indicating whether
        this boot environment is fully UEFI-compliant.

      - a1 = cmdline: `cmdline` is a pointer to the kernel command line.

      - a2 = systemtable: `systemtable` points to the EFI system table.
        All pointers involved at this stage are in physical addresses.

Header of Linux/LoongArch kernel images
=======================================

Linux/LoongArch kernel images are EFI images. Being PE files, they have
a 64-byte header structured like::

	u32	code0                   /* Executable code, contains "MZ" for EFI STUB */
	u32	code1                   /* Executable code */
	u64	kernel_entry            /* Kernel entry point  */
	u64	_end - _text            /* Effective Image size */
	u64	load_offset             /* Kernel image load offset from start of RAM */
	u32	flags                   /* Image flags */
	u32	version                 /* Version */
	u64	res1 = 0                /* Reserved */
	u64	res2 = 0                /* Reserved */
	u32	magic                   /* Magic (LINUX_PE_MAGIC for EFI Stub or "LA32"/"LA64") */
	u32	pe_header - _head       /* Offset to the PE header if exists, otherwise 0 */

Notes
=====

- This header is also reused to support EFI stub for LoongArch. When the kernel
  is built with EFI stub, code0 should contain the "MZ" magic number and magic
  field will be set to LINUX_PE_MAGIC. In this case Bootloader should look into
  PE header to validate kernel image.

- When the kernel is built without EFI stub, code0 can be any machine code, and
  magic field will be set to "LA32" or "LA64". Bootloader should solely rely on
  the magic field to validate kernel image.

- The flags field (introduced in v0.1) is a little-endian 32-bit field
  composed as follows:

  ============= ===============================================================
  Bit 0		Kernel endianness.  1 if BE, 0 if LE.
  Bit 1-2	Kernel Page size.

			* 0 - Unspecified.
			* 1 - 4K
			* 2 - 16K
			* 3 - 64K
  Bit 3		Kernel relocatable

			0
			  Kernel is not relocatable. Kernel should be loaded
			  at the base address specified by the load_offset.
			1
			  Kernel is relocatable. Kernel can be loaded at any
			  location aligned to the page size.
  Bits 4-31	Reserved.
  ============= ===============================================================


- version field indicate header version number

	==========  =============
	Bits 0:15   Minor version
	Bits 16:31  Major version
	==========  =============

  This preserves compatibility across newer and older version of the header.
  The current version is defined as 0.1.

  Version 0.1 added support for raw boot image without EFI stub.
