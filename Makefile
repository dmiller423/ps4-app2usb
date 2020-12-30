TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJNAME    := $(shell basename $(CURDIR))
#$(notdir $(shell pwd)) 
INTDIR      := $(CURDIR)/build
SRCDIR		:= $(CURDIR)/source
IMGUIDIR	:= $(SRCDIR)/imgui
UISRCDIR	:= $(SRCDIR)/ui

# Libraries linked into the ELF.
LIBS        := -lc -lkernel -lc++ -lSceVideoOut -lSceUserService -lScePad

# Compiler options. You likely won't need to touch these.
UNAME_S     := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
		CC      := clang++
		LD      := ld.lld
		CDIR    := linux
endif
ifeq ($(UNAME_S),Darwin)
		CC      := /usr/local/opt/llvm/bin/clang++
		LD      := /usr/local/opt/llvm/bin/ld.lld
		CDIR    := macos
endif
ODIR        := $(INTDIR)
SDIR        := $(SRCDIR)
IDIRS       := -I$(TOOLCHAIN)/include -I$(TOOLCHAIN)/include/c++/v1
LDIRS       := -L$(TOOLCHAIN)/lib
## this is annoying at best, why i was using -Xclang, need some frontend opts like cpu/tune/avx2/bmi
CFLAGS      := -cc1 -O2 -triple x86_64-pc-freebsd-elf -munwind-tables $(IDIRS) -fuse-init-array -debug-info-kind=limited -debugger-tuning=gdb -emit-obj -D_OO_
LFLAGS      := -O2 -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr $(LDIRS) $(LIBS) $(TOOLCHAIN)/lib/crt1.o

CFILES      := $(wildcard $(SDIR)/*.c)
CPPFILES    := $(wildcard $(SDIR)/*.cpp)
UISRCFILES  := $(wildcard $(UISRCDIR)/*.cpp)
IMGUIFILES  := $(wildcard $(IMGUIDIR)/*.cpp)
OBJS        := $(patsubst $(SDIR)/%.c, $(ODIR)/%.o, $(CFILES)) $(patsubst $(SDIR)/%.cpp, $(ODIR)/%.o, $(CPPFILES)) $(patsubst $(IMGUIDIR)/%.cpp, $(ODIR)/%.o, $(IMGUIFILES)) $(patsubst $(UISRCDIR)/%.cpp, $(ODIR)/%.o, $(UISRCFILES))

TARGET = eboot.bin

# Create the intermediate directory incase it doesn't already exist.
_unused := $(shell mkdir -p $(INTDIR))

# Make rules
$(TARGET): $(ODIR) $(OBJS)
	$(LD) $(ODIR)/*.o -o $(ODIR)/$(PROJNAME).elf $(LFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-eboot -in=$(ODIR)/$(PROJNAME).elf -out=$(ODIR)/$(PROJNAME).oelf --paid 0x3800000000000011

$(ODIR)/%.o: $(SDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

$(ODIR)/%.o: $(SDIR)/%.cpp
	$(CC) $(CFLAGS) -o $@ $<

$(ODIR)/%.o: $(UISRCDIR)/%.cpp
	$(CC) $(CFLAGS) -o $@ $<


$(ODIR)/%.o: $(IMGUIDIR)/%.cpp
	$(CC) $(CFLAGS) -o $@ $<

$(ODIR):
	@mkdir $@

.PHONY: clean

clean:
	echo "project name $(PROJNAME), imgui dir $(IMGUIDIR) ;; files $(IMGUIFILES)"
	rm -f $(TARGET) $(ODIR)/*
