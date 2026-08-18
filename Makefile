TARGET_NAME ?= zpk

ifeq ($(OS),Windows_NT)
HOST := windows
else
HOST := posix
endif

# target defaults to HOST, do `make PLATFORM=windows` for cross-compiling.
PLATFORM ?= $(HOST)

BUILD_DIR ?= ./build/$(PLATFORM)
SRC_DIRS ?= src vendor platsrc/$(PLATFORM)

SRCS := $(shell find $(SRC_DIRS) -type f -name *.c)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_DIRS := vendor src
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

DEFINES := -DMZ_PLATFORM=0 -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES

LDFLAGS := -lm 

CC := clang
CFLAGS ?= $(INC_FLAGS) $(DEFINES) -std=c2y -fdefer-ts -MMD -MP -O0 -g3 -Wall -Weverything -pedantic \
 -Wno-padded -Wno-switch-enum -Wno-declaration-after-statement -Wno-unsafe-buffer-usage \
 -Wno-implicit-void-ptr-cast -Wno-pre-c2y-compat -Wno-pre-c23-compat -Wno-pre-c11-compat \
 -Wno-switch-default


ifeq ($(PLATFORM),windows)
CROSSFLAGS := --target=x86_64-w64-windows-gnu
CFLAGS += $(CROSSFLAGS)
LDFLAGS += $(CROSSFLAGS)
endif

ifeq ($(PLATFORM),posix)
CFLAGS  += -fsanitize=address
LDFLAGS += -fsanitize=address
endif

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vendor/apkver/%.c.o: CFLAGS += -Wno-variadic-macros \
 -Wno-implicit-fallthrough -Wno-sign-compare -Wno-gnu-case-range

$(BUILD_DIR)/vendor/miniz/%.c.o: CFLAGS += -D_DEFAULT_SOURCE \
 -Wno-sign-conversion -Wno-comma -Wno-extra-semi-stmt -Wno-implicit-int-conversion \
 -Wno-cast-qual -Wno-unused-macros -Wno-switch-default -Wno-covered-switch-default

$(BUILD_DIR)/vendor/bearssl/%.c.o: CFLAGS += \
 -Ivendor/bearssl/inc -Ivendor/bearssl/src -w

$(BUILD_DIR)/vendor/llrb/%.c.o: CFLAGS += -Wno-variadic-macros -Wno-unused-function 

ifeq ($(PLATFORM),windows)
TARGET_EXEC ?= $(TARGET_NAME).exe 
else
TARGET_EXEC ?= $(TARGET_NAME)
endif

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)

DEPS := $(OBJS:.o=.d)

-include $(DEPS)

MKDIR_P ?= mkdir -p
