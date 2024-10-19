CFLAGS = -Wall -Wextra -fuse-ld=lld
CFLAGS_OPT = -s -mwindows -municode -D_WINDOWS -DNDEBUG -march=native -Ofast -fomit-frame-pointer -ffunction-sections -fdata-sections -fmerge-all-constants -flto -Wl,-O1 -Wl,--sort-common -Wl,--gc-sections -Wl,--icf=all
CFLAGS_DBG = -g -D_CONSOLE -D_DEBUG -D_UNICODE -DUNICODE
SRC = WinKodiG20.c
LIBS = -lcfgmgr32 -lhid

default: WinKodiG20.exe

debug: WinKodiG20_d.exe

WinKodiG20.exe: $(SRC)
	clang $(CFLAGS) $(CFLAGS_OPT) $^ $(LIBS) -o $@
	strip $@

WinKodiG20_d.exe: $(SRC)
	clang $(CFLAGS) $(CFLAGS_DBG) $^ $(LIBS) -o $@

clean:
	rm -f WinKodiG20*.exe

.PHONY: clean debug