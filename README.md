# GIB
Gibsy's Image Binary
# BUILD
### Build on Windows (MSYS2)
1. Install MSYS2
2. Install dependencies
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-libpng \
          mingw-w64-ucrt-x86_64-zstd \
          mingw-w64-ucrt-x86_64-SDL2 \
          mingw-w64-ucrt-x86_64-libjpeg-turbo \
          make
```
3. Clone and build
4. Build exe
```bash
gcc -O2 -std=c11 -o img2gib.exe img2gib.c \
  -static -lzstd -lpng -ljpeg -lz -lm

gcc -O2 -std=c11 $(sdl2-config --cflags) -o gib_viewer.exe gib_viewer.c \
  -static -lzstd -lpng -lz -lm \
  $(sdl2-config --static-libs)
```
### Build on linux
```bash
sudo apt install gcc libpng-dev libzstd-dev libsdl2-dev libjpeg-dev make
make
```
### Build on Mac
```bash
brew install libpng zstd sdl2 libjpeg
make
```
