#
# arch/loongarch/boot/Makefile
#
# Copyright (C) 2020-2022 Loongson Technology Corporation Limited
#

drop-sections := .comment .note .options .note.gnu.build-id
strip-flags   := $(addprefix --remove-section=,$(drop-sections)) -S
OBJCOPYFLAGS_vmlinux.efi := -O binary $(strip-flags)

quiet_cmd_strip = STRIP	  $@
      cmd_strip = $(STRIP) -s -o $@ $<

targets := vmlinux.elf
$(obj)/vmlinux.elf: vmlinux FORCE
	$(call if_changed,strip)

targets += Image Image.*
$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/Image.gz: $(obj)/Image FORCE
	$(call if_changed,gzip)

$(obj)/Image.bz2: $(obj)/Image FORCE
	$(call if_changed,bzip2)

$(obj)/Image.lz4: $(obj)/Image FORCE
	$(call if_changed,lz4)

$(obj)/Image.lzma: $(obj)/Image FORCE
	$(call if_changed,lzma)

$(obj)/Image.lzo: $(obj)/Image FORCE
	$(call if_changed,lzo)

$(obj)/Image.zst: $(obj)/Image FORCE
	$(call if_changed,zstd)

$(obj)/Image.xz: $(obj)/Image FORCE
	$(call if_changed,xzkern)

targets += vmlinux.efi
$(obj)/vmlinux.efi: vmlinux FORCE
	$(call if_changed,objcopy)

EFI_ZBOOT_PAYLOAD      := vmlinux.efi
EFI_ZBOOT_BFD_TARGET   := elf64-loongarch
EFI_ZBOOT_MACH_TYPE    := LOONGARCH64

include $(srctree)/drivers/firmware/efi/libstub/Makefile.zboot
