# RioOS x86_64 — Termux + QEMU + Termux:X11

Versi ini menjalankan shell bare-metal x86-64 dengan kernel **RioKernel** di jendela QEMU melalui SDL/Termux:X11. Tidak menggunakan Linux di dalam guest dan tidak membutuhkan GRUB.

## 1. Persiapan satu kali

1. Pasang aplikasi **Termux:X11** dari halaman rilis resminya: <https://github.com/termux/termux-x11/releases>.
2. Buka Termux dan masuk ke folder proyek.
3. Jalankan `run.sh`. Script akan memasang paket Termux yang diperlukan saat belum tersedia (`clang`, `lld`, `binutils`, repository X11, QEMU grafis, dan command Termux:X11).

Termux:X11 Android app harus sudah terpasang. Paket `termux-x11-nightly` menyediakan command-line server/client, bukan APK Android-nya.

## 2. Build dan jalankan

Dari Termux:

```sh
cd /path/ke/folder/gabut
chmod +x run.sh
./run.sh
```

Atau:

```sh
bash run.sh
```

`run.sh` melakukan seluruh proses berikut secara otomatis:

- Mengompilasi BIOS boot sector 16-bit.
- Mengompilasi entry stub 32-bit yang mengaktifkan paging dan masuk ke long mode.
- Mengompilasi kernel C x86-64, VGA console, keyboard PS/2, library string, RTC, environment, ramfs, perintah Unix-like, dan shell.
- Melakukan link kernel ELF64 dan mengubahnya menjadi payload BIOS.
- Membuat `build/rioos.img` (boot sector + kernel).
- Menyalakan Termux:X11 pada display `:0` bila belum aktif.
- Menjalankan `qemu-system-x86_64` dengan backend SDL agar shell tampil di jendela X11.

Setiap kali `run.sh` dijalankan, source dikompilasi ulang.

## 3. Shell RioOS (riosh)

Shell menyediakan 68 perintah dengan argumen, prompt dinamis, alias, ekspansi environment, dan riwayat 16 perintah. Ketik `help` untuk daftar singkat atau `man COMMAND` untuk detail penggunaan.

Fitur:

- Prompt `user@host:dir$` dengan `~` untuk direktori home.
- Kutip tunggal/ganda, misalnya `echo "hello world"` atau `alias ll='ls -l'`.
- Ekspansi `$USER`, `$HOME`, `${PWD}`, dan variabel lain.
- Alias bawaan `ll` = `ls -l`; buat alias dengan `alias nama='perintah'`.
- Filesystem in-memory `ramfs` (32 KiB) berisi `/home/rio`, `/etc`, `/tmp`, `/var/log`, `/bin`, dan `/usr/bin`.

Perintah informasi dan sistem:

| Perintah | Fungsi |
|---|---|
| `about` | Informasi OS |
| `version` | Versi dan arsitektur |
| `uname [-a] [-s] [-n] [-r] [-m]` | Informasi sistem |
| `cpuinfo` | Vendor dan model CPU melalui CPUID |
| `mem` | RAM guest dan pemakaian ramfs |
| `free` | Statistik memori |
| `df` | Pemakaian filesystem ramfs |
| `uptime` | Waktu aktif sistem |
| `date [+FORMAT]` | Tanggal/jam RTC dengan format `%Y-%m-%d` |
| `cal [MONTH] [YEAR]` | Kalender |
| `neofetch` | Ringkasan sistem sederhana |
| `hostname [NAME]` | Lihat atau ubah hostname |
| `whoami` | Nama user saat ini |
| `id` | UID, GID, dan grup |
| `tty` | Nama terminal |
| `ps` | Daftar task |
| `mount` | Filesystem yang terpasang |
| `lspci` | Perangkat PCI dari QEMU |

Perintah file dan direktori:

| Perintah | Fungsi |
|---|---|
| `ls [-l] [-a] [PATH]` | Daftar isi direktori |
| `cd [PATH]` | Pindah direktori (`cd`, `cd -`, `cd ~`) |
| `pwd` | Cetak direktori kerja |
| `cat [-n] FILE...` | Tampilkan isi file |
| `head [-n N] FILE...` | Awal file |
| `tail [-n N] FILE...` | Akhir file |
| `more FILE` | Tampilan per halaman |
| `wc [-l] [-w] [-c] FILE...` | Hitung baris, kata, byte |
| `grep [-i] [-v] [-n] PAT FILE...` | Cari teks |
| `sort [-r] [-n] FILE` | Urutkan baris |
| `rev FILE` | Balik setiap baris |
| `tree [PATH]` | Pohon direktori |
| `find [PATH] [-name PATTERN]` | Cari file rekursif |
| `du [-s] [PATH]` | Estimasi ukuran |
| `stat FILE` | Status file |
| `touch PATH...` | Buat file kosong |
| `mkdir [-p] PATH...` | Buat direktori |
| `rmdir PATH...` | Hapus direktori kosong |
| `rm [-r] [-f] PATH...` | Hapus file/direktori |
| `cp [-r] SRC DST` | Salin |
| `mv SRC DST` | Pindah/nama ulang |
| `basename PATH [SUFFIX]` | Ambil nama file |
| `dirname PATH` | Ambil direktori induk |
| `hexdump FILE` | Dump heksadesimal |

Perintah teks dan matematika:

| Perintah | Fungsi |
|---|---|
| `echo [-n] [-e] [TEXT...]` | Cetak argumen |
| `printf FORMAT [ARG...]` | Format `%s`, `%d`, `%x`, dan escape `\n` |
| `seq [FIRST [STEP]] LAST` | Deret angka |
| `expr A OP B` | Aritmetika integer (`+ - * / %`) |
| `factor NUMBER` | Faktorisasi prima |
| `true`, `false` | Status sukses/gagal |
| `sleep SECONDS` | Tunda eksekusi |
| `yes [TEXT...]` | Cetak berulang sampai tombol ditekan |

Perintah shell dan environment:

| Perintah | Fungsi |
|---|---|
| `help [COMMAND]` | Daftar perintah |
| `man COMMAND` | Detail penggunaan |
| `which NAME...` | Lokasi perintah |
| `type NAME...` | Jenis perintah atau alias |
| `alias [NAME[=VALUE]...]` | Buat/daftar alias |
| `unalias NAME...`, `unalias -a` | Hapus alias |
| `history [-c]` | Riwayat 16 perintah |
| `env` | Daftar environment |
| `printenv [NAME...]` | Cetak environment |
| `unset NAME...` | Hapus environment |
| `clear`, `cls` | Bersihkan layar |
| `exit` | Keluar dan halt CPU |

Kontrol sistem dan tampilan:

| Perintah | Fungsi |
|---|---|
| `color FG [BG]` | Ubah warna teks dan latar |
| `reboot` | Restart VM |
| `poweroff` | Matikan VM melalui ACPI |
| `halt` | Hentikan CPU virtual |

Warna yang tersedia: `black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `gray`, `darkgray`, `lightblue`, `lightgreen`, `lightcyan`, `lightred`, `pink`, `yellow`, `white`.

## 4. Catatan teknis dan batasan

- Guest menggunakan BIOS boot dari hard-disk image IDE, bukan opsi `qemu -kernel`.
- Boot sector membaca payload kernel dari LBA 1–127 ke alamat fisik `0x10000`; karena itu payload dibatasi maksimum 65.024 byte (payload saat ini sekitar 58 KB).
- Kernel membangun identity map 1 GiB dengan page berukuran 2 MiB, lalu berpindah ke x86-64 long mode.
- Tampilan saat ini adalah **VGA text console 80×25 di jendela QEMU**, bukan desktop GUI penuh.
- Input keyboard memakai controller PS/2 emulasi QEMU. Klik jendela QEMU agar keyboard mendapat fokus.
- RAM guest diatur 128 MiB.
- Filesystem `ramfs` hanya berada di memori: perubahan hilang saat reboot dan tidak ada driver disk atau partisi nyata.
- Belum ada pipe (`|`) dan redirection (`>`, `<`); semua perintah menulis langsung ke konsol.
