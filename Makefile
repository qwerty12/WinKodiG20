SRC = WinKodiG20.c BrightnessControl.c
LIBS = -lcfgmgr32 -lhid -ldxva2
MANIFEST = WinKodiG20.exe.manifest
CFLAGS = -Wall -Wextra -fuse-ld=lld -Wl,/manifest:EMBED -Wl,/manifestuac:no -Wl,/manifestinput:$(MANIFEST)
CFLAGS_OPT = -s -mwindows -municode -D_WINDOWS -DNDEBUG -march=native -O3 -ffast-math -fomit-frame-pointer -ffunction-sections -fdata-sections -fmerge-all-constants -flto -Wl,-O1 -Wl,--sort-common -Wl,--gc-sections -Wl,--icf=all
CFLAGS_DBG = -g -D_CONSOLE -D_DEBUG -D_UNICODE -DUNICODE

default: WinKodiG20.exe

debug: WinKodiG20_d.exe

WinKodiG20.exe: $(SRC) $(MANIFEST)
	clang $(CFLAGS) $(CFLAGS_OPT) $(SRC) $(LIBS) -o $@
	strip $@

WinKodiG20_d.exe: $(SRC) $(MANIFEST)
	clang $(CFLAGS) $(CFLAGS_DBG) $(SRC) $(LIBS) -o $@

clean:
	rm -f WinKodiG20*.exe

.PHONY: clean debug