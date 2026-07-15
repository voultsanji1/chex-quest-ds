# Chocolate Doom DS - GDB configuration
# Runs inside Docker via: make -f Makefile.nds gdb
# Connects to melonDS GDB stub on the host.
#
# melonDS setup (one-time):
#   Settings -> Devtools -> Enable GDB stub
#   ARM9 port: 3333 (default)
#   Disable JIT recompiler
#   Check "Break on startup" for catching main()

set confirm off
set pagination off
mem inaccessible-by-default off

file build/chocolate_doom_ds.elf

# Connect to melonDS on the host machine.
# host.docker.internal resolves to the host IP from inside Docker
# (requires extra_hosts in docker-compose.yml).
target remote host.docker.internal:3333

# NDS memory regions
mem 0x02000000 0x02400000 rw   # Main RAM (4 MB)
mem 0x04000000 0x04001000 rw   # I/O registers
mem 0x05000000 0x05000800 rw   # Palette RAM
mem 0x06000000 0x06898000 rw   # VRAM (all banks)
mem 0x07000000 0x07000800 rw   # OAM

# Convenience commands

define nds-regs
  info registers
end
document nds-regs
  Display ARM9 register state.
end

define nds-bt
  backtrace 20
end
document nds-bt
  Show backtrace (20 frames).
end

define nds-vram
  x/16h 0x06000000
end
document nds-vram
  Dump first 16 pixels of VRAM_A (framebuffer top-left).
end

define nds-zone
  print Z_FreeMemory()
  print Z_ZoneSize()
end
document nds-zone
  Show Doom zone allocator free/total memory.
end

define nds-stack
  x/32w $sp
end
document nds-stack
  Show 32 words at current stack pointer.
end

echo \n
echo Chocolate Doom DS GDB ready.\n
echo Commands: nds-regs, nds-bt, nds-vram, nds-zone, nds-stack\n
echo \n
