# GIB
Gibsy's Image Binary - image format using ZSTD
# BUILD
### Build on Windows (MSYS2)
1. Install MSYS2 and open it
2. Install dependencies
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-libpng \
          mingw-w64-ucrt-x86_64-zstd \
          mingw-w64-ucrt-x86_64-SDL2 \
          mingw-w64-ucrt-x86_64-libjpeg-turbo 
```
3. Clone and build
```
git clone https://github.com/Gibsy/Gibsy-Image-Binary
cd Gibsy-Image-Binary
make
```
4. Build exe
```bash
gcc -O2 -std=c11 -o img2gib.exe img2gib.c \
  -static -lzstd -lpng -ljpeg -lz -lm

gcc -O2 -std=c11 $(sdl2-config --cflags) -o gib_viewer.exe gib_viewer.c \
  -static -lzstd -lpng -lz -lm \
  $(sdl2-config --static-libs)
```
> remove -static if you get linker errors
> 
> you can install using make but this requires DLLs in path (don't forget pacman -S make)
### Build on linux
```bash
sudo apt install gcc libpng-dev libzstd-dev libsdl2-dev libjpeg-dev make
make
```
### Build on Mac
```bash
brew install gcc libpng zstd sdl2 libjpeg make
make
```
