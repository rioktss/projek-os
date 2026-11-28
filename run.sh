#!/usr/bin/env bash
# RioOS x86_64 — build from source and launch QEMU in a graphical window.
# Works both in Termux (Termux:X11) and on a normal Linux desktop (e.g. Arch).
set -Eeuo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"

say() { printf '\033[1;36m[RioOS]\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m[RioOS ERROR]\033[0m %s\n' "$*" >&2; exit 1; }

X11_LOG="${TMPDIR:-/tmp}/rioos-x11.log"
IN_TERMUX=0
if [ -d /data/data/com.termux ] && command -v pkg >/dev/null 2>&1; then
    IN_TERMUX=1
fi

# Install the required Termux packages automatically when run inside Termux.
if [ "$IN_TERMUX" -eq 1 ]; then
    if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1 || \
       ! command -v llvm-objcopy >/dev/null 2>&1; then
        say "Installing compiler/linker tools..."
        pkg install -y clang lld binutils
    fi

    if ! command -v qemu-system-x86_64 >/dev/null 2>&1 || \
       ! qemu-system-x86_64 -display help 2>&1 | grep -qi 'sdl'; then
        say "Enabling the Termux:X11 repository and installing graphical QEMU..."
        pkg install -y x11-repo
        pkg update -y
        pkg install -y qemu-system-x86-64
    fi

    if ! command -v termux-x11 >/dev/null 2>&1; then
        say "Installing the Termux:X11 command-line package..."
        pkg install -y x11-repo termux-x11-nightly
    fi
fi

install_hint() {
    if command -v pacman >/dev/null 2>&1; then
        printf "sudo pacman -S clang lld binutils qemu-system-x86"
    else
        printf "a C compiler, lld, binutils and qemu-system-x86-64"
    fi
}

for tool in clang ld.lld qemu-system-x86_64; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        fail "Tool '$tool' is missing. Install it with: $(install_hint)"
    fi
done

if command -v llvm-objcopy >/dev/null 2>&1; then
    OBJCOPY="$(command -v llvm-objcopy)"
elif command -v objcopy >/dev/null 2>&1; then
    OBJCOPY="$(command -v objcopy)"
else
    fail "llvm-objcopy/objcopy is missing. Install it with: $(install_hint)"
fi

mkdir -p build

say "[1/5] Compiling 512-byte BIOS boot sector..."
clang --target=i386-elf -c -ffreestanding -fno-pic -fno-pie \
    boot/bootsector.S -o build/bootsector.o
ld.lld -m elf_i386 -T boot/bootsector.ld \
    -o build/bootsector.elf build/bootsector.o
"$OBJCOPY" -O binary build/bootsector.elf build/bootsector.bin

BOOT_SIZE="$(wc -c < build/bootsector.bin | tr -d ' ')"
[ "$BOOT_SIZE" = "512" ] || fail "Boot sector must be exactly 512 bytes; got ${BOOT_SIZE}."

say "[2/5] Compiling kernel, drivers, filesystem, and shell..."
COMMON_CFLAGS=(
    --target=x86_64-elf -std=gnu11 -Os -Wall -Wextra -Werror
    -ffreestanding -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie -fno-asynchronous-unwind-tables
    -fno-unwind-tables -fno-strict-aliasing
    -m64 -mcmodel=small -mno-red-zone -mno-mmx -mno-sse -mno-sse2
    -msoft-float -Iinclude
)

clang --target=x86_64-elf -ffreestanding -fno-pic -fno-pie -m64 \
    -mno-red-zone -c boot/boot.S -o build/boot.o
clang "${COMMON_CFLAGS[@]}" -c kernel/kernel.c -o build/kernel.o
clang "${COMMON_CFLAGS[@]}" -c drivers/vga.c -o build/vga.o
clang "${COMMON_CFLAGS[@]}" -c drivers/keyboard.c -o build/keyboard.o
clang "${COMMON_CFLAGS[@]}" -c drivers/time.c -o build/time.o
clang "${COMMON_CFLAGS[@]}" -c drivers/pci.c -o build/pci.o
clang "${COMMON_CFLAGS[@]}" -c drivers/virtio_gpu.c -o build/virtio_gpu.o
clang "${COMMON_CFLAGS[@]}" -c fs/ramfs.c -o build/ramfs.o
clang "${COMMON_CFLAGS[@]}" -c lib/lib.c -o build/lib.o
clang "${COMMON_CFLAGS[@]}" -c shell/env.c -o build/env.o
clang "${COMMON_CFLAGS[@]}" -c shell/unix.c -o build/unix.o
clang "${COMMON_CFLAGS[@]}" -c shell/shell.c -o build/shell.o

say "[3/5] Linking ELF64 kernel..."
ld.lld -m elf_x86_64 -nostdlib -T linker.ld \
    -o build/riokernel.elf build/boot.o build/kernel.o build/vga.o \
    build/keyboard.o build/lib.o build/time.o build/pci.o build/virtio_gpu.o \
    build/env.o build/ramfs.o build/unix.o build/shell.o

say "[4/5] Creating BIOS hard-disk image..."
"$OBJCOPY" -O binary build/riokernel.elf build/riokernel.bin
KERNEL_SIZE="$(wc -c < build/riokernel.bin | tr -d ' ')"
MAX_KERNEL_SIZE=$((127 * 512))
if [ "$KERNEL_SIZE" -gt "$MAX_KERNEL_SIZE" ]; then
    fail "Kernel is ${KERNEL_SIZE} bytes; BIOS loader limit is ${MAX_KERNEL_SIZE} bytes."
fi

# LBA 0 = boot sector. LBA 1..127 = kernel payload and zero padding.
dd if=/dev/zero of=build/rioos.img bs=512 count=128 status=none
dd if=build/bootsector.bin of=build/rioos.img conv=notrunc status=none
dd if=build/riokernel.bin of=build/rioos.img bs=512 seek=1 conv=notrunc status=none

say "Kernel payload: ${KERNEL_SIZE} bytes"

# Pick a QEMU display backend. Termux/X11 uses SDL; a Wayland-only Linux
# desktop usually needs GTK. Override with RIO_DISPLAY=<backend>, e.g.
# RIO_DISPLAY=sdl ./run.sh
if [ -n "${RIO_DISPLAY:-}" ]; then
    DISPLAY_BACKEND="$RIO_DISPLAY"
elif [ "$IN_TERMUX" -eq 1 ] || [ -n "${DISPLAY:-}" ]; then
    DISPLAY_BACKEND=sdl
else
    DISPLAY_BACKEND=gtk
fi

if [ "$IN_TERMUX" -eq 1 ]; then
    command -v termux-x11 >/dev/null 2>&1 || \
        fail "Termux:X11 is not installed. Install the Termux:X11 Android app and run: pkg install x11-repo termux-x11-nightly"
    # Termux:X11 uses display :0 by default. Start its server if available.
    export DISPLAY="${DISPLAY:-:0}"
    if command -v pgrep >/dev/null 2>&1; then
        if ! pgrep -f 'termux-x11 :0' >/dev/null 2>&1; then
            termux-x11 :0 -legacy-drawing >"$X11_LOG" 2>&1 &
            sleep 2
        fi
    else
        termux-x11 :0 -legacy-drawing >"$X11_LOG" 2>&1 &
        sleep 2
    fi
else
    case "$DISPLAY_BACKEND" in
        none|vnc=*|vnc) ;; # no local display needed
        *)
            [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] || \
                fail "No graphical display found. Log in to X11/Wayland, or set RIO_DISPLAY=none for headless."
            ;;
    esac
fi

# The virtio-gpu device opens its own 1280x800 window in QEMU.  It is off by
# default so only the VGA text console (the shell) is visible.  Set RIO_GPU=1
# to bring the framebuffer back, e.g. to use gpuinfo/gputest/gpuclr/gpudraw.
QEMU_ARGS=(
    -machine pc,accel=tcg
    -cpu qemu64
    -m 128M
    -vga std
    -drive file=build/rioos.img,format=raw,if=ide,index=0,media=disk
    -boot order=c
    -display "$DISPLAY_BACKEND"
    -name "RioOS x86_64"
)
if [ "${RIO_GPU:-0}" = "1" ]; then
    QEMU_ARGS+=(-device virtio-gpu-pci,xres=1280,yres=800)
fi

say "[5/5] Starting QEMU x86_64 in a graphical window (${DISPLAY_BACKEND}, GPU=$([ "${RIO_GPU:-0}" = "1" ] && echo on || echo off))..."
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
