# libdragon build scaffold for the N64 port of VVVVVV.
#
# This Makefile follows the structure used by the libdragon example projects:
#   1. include the libdragon build rules from n64.mk,
#   2. convert raw assets and stage them into the filesystem root,
#   3. build a DragonFS image,
#   4. build an ELF for the game,
#   5. package the final .z64 ROM.
#
# The implementation is intentionally incremental: the actual runtime shims for
# rendering, audio, input, and filesystem access are not wired in yet. This
# file defines the build pipeline and keeps the project organized for that work.

PROJECT_NAME := vvvvvv64
N64_ROM_TITLE ?= "VVVVVV 64"
BUILD_DIR ?= build
SRC_DIR ?= desktop_version/src

# Prefer the installed libdragon SDK when available, but fall back to the
# vendored submodule copy so the repository remains usable in a checkout.
N64_MK ?= $(N64_INST)/include/n64.mk
ifeq ($(wildcard $(N64_MK)),)
N64_MK := $(CURDIR)/third_party/libdragon/n64.mk
endif
include $(N64_MK)

PATH_TO_OG_GAME ?= $(CURDIR)/third_party/VVVVVV

# The raw assets that will be converted later live under the repository's
# existing assets/ and data/ trees. The staging directory mirrors the layout
# expected by DragonFS and the later asset-conversion rules.
ASSET_INPUTS := assets data

ASSETS_MUSIC := $(wildcard data/music/*.mp3)
ASSETS_SFX := $(wildcard data/sounds/*.wav)
ASSETS_GFX := $(wildcard data/graphics/*.png)
ASSETS_LVLS := $(wildcard data/levels/*.vvvvvv)
ASSETS_FONTS := $(wildcard assets/fonts/*.fnt)
ASSETS_LANG := $(wildcard assets/lang/**/*.xml)

FS_MUSIC := $(patsubst data/music/%, $(N64_MKDFS_ROOT)/music/%, $(ASSETS_MUSIC:%.mp3=%.wav64))
FS_SFX := $(patsubst data/sounds/%, $(N64_MKDFS_ROOT)/sounds/%, $(ASSETS_SFX:%.wav=%.wav64))
FS_GFX := $(patsubst data/graphics/%, $(N64_MKDFS_ROOT)/graphics/%, $(ASSETS_GFX:%.png=%.sprite))
FS_LVLS := $(patsubst data/levels/%, $(N64_MKDFS_ROOT)/levels/%, $(ASSETS_LVLS:%.vvvvvv=%.vvvvvv))
FS_FONTS := $(patsubst assets/fonts/%, $(N64_MKDFS_ROOT)/fonts/%, $(ASSETS_FONTS:%.fnt=%.font64))
FS_LANG := $(patsubst assets/lang/%, $(N64_MKDFS_ROOT)/lang/%, $(ASSETS_LANG:%.xml=%.xml))

all: $(PROJECT_NAME).z64

# Step 0: Compile tools for the asset pipeline and extract the raw assets from the original VVVVVV game. The tools are built from the source in tools/ and the raw assets are extracted from the original game data files.


# Step 1: Create the filesystem root used by the asset pipeline.
# We use the standard definition from n64.mk so it goes into filesystem/.
$(N64_MKDFS_ROOT): $(ASSET_INPUTS)
	@mkdir -p $@
	@echo "    [ASSETS] N64 filesystem root prepared at $(N64_MKDFS_ROOT)"

# Step 2: Convert the assets and stage them into the filesystem root.
# This target is intentionally simple right now; it reserves the directory
# structure for the later conversion rules without claiming that the assets are
# already converted.

$(N64_MKDFS_ROOT)/music/%.wav64: data/music/%.mp3
	@mkdir -p "$(@D)"
	@echo "    [AUDIOCONV64 (MUSIC)] $< -> $@"
	@$(N64_AUDIOCONV) --wav-compress 2 --wav-loop true -o "$(dir $@)" "$<"

$(N64_MKDFS_ROOT)/sounds/%.wav64: data/sounds/%.wav
	@mkdir -p "$(@D)"
	@echo "    [AUDIOCONV64 (SFX)] $< -> $@"
	@$(N64_AUDIOCONV) --wav-compress 1 -o "$(dir $@)" "$<"

$(N64_MKDFS_ROOT)/graphics/%.sprite: data/graphics/%.png
	@mkdir -p "$(@D)"
	@echo "    [MKSPRITE] $< -> $@"
	@$(N64_MKSPRITE) -o "$(dir $@)" "$<"

$(N64_MKDFS_ROOT)/levels/%.vvvvvv: data/levels/%.vvvvvv
	@mkdir -p "$(@D)"
	@echo "    [MKASSET (LVL)] $< -> $@"
	$(N64_BINDIR)/mkasset -o "$(dir $@)" "$<"

$(N64_MKDFS_ROOT)/fonts/%.font64: assets/fonts/%.fnt
	@mkdir -p "$(@D)"
	@echo "    [MKFONT] $< -> $@"
	@$(N64_BINDIR)/mkfont -o "$(dir $@)" "$<"

$(N64_MKDFS_ROOT)/lang/%.xml: assets/lang/%.xml
	@mkdir -p "$(@D)"
	@echo "    [MKASSET (LANG)] $< -> $@"
	$(N64_BINDIR)/mkasset -o "$(dir $@)" "$<"

convert-music: $(FS_MUSIC)

convert-sfx: $(FS_SFX)

convert-gfx: $(FS_GFX)

convert-lvls: $(FS_LVLS)

convert-fonts: $(FS_FONTS)

convert-lang: $(FS_LANG)

convert-assets: $(N64_MKDFS_ROOT) convert-music convert-sfx convert-gfx convert-lvls convert-fonts convert-lang

# Step 3: Build a DragonFS image from the staged files.
# The generic libdragon rule in n64.mk will create the DFS file, we just need to
# declare its dependencies to populate the filesystem tree before this
# target is executed.
$(BUILD_DIR)/$(PROJECT_NAME).dfs: convert-assets

# Step 4: Build the ELF for the ported game.
# The generic libdragon rules will compile all required .c or .cpp files into .o files.
# We just need to list the source files and use the list to generate a list of object files for the ELF target.
# This is intentionally a placeholder rule for now. Once the game is adapted to
# libdragon, the ported source files should be added to PORT_SRCS and compiled.
PORT_SRCS :=
PORT_OBJS :=

# The generic libdragon rule in n64.mk will build the ELF from the object files, we just need to declare its dependencies.
$(BUILD_DIR)/$(PROJECT_NAME).elf: $(PORT_OBJS)

# Step 5: Package the final ROM. The generic libdragon rule in n64.mk will use
# the ELF and DFS files to produce the .z64 output.
$(PROJECT_NAME).z64: $(BUILD_DIR)/$(PROJECT_NAME).elf $(BUILD_DIR)/$(PROJECT_NAME).dfs

clean:
	rm -rf $(BUILD_DIR) $(PROJECT_NAME).z64
	rm -rf $(N64_MKDFS_ROOT)

.PHONY: all convert-music convert-sfx convert-gfx convert-lvls convert-fonts convert-lang convert-assets clean
