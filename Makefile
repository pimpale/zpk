TARGET_EXEC ?= zpk

BUILD_DIR ?= ./build
SRC_DIRS ?= src vendor

SRCS := $(shell find $(SRC_DIRS) -type f -name *.c)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_DIRS := vendor src
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

DEFINES := -DMZ_PLATFORM=0

LDFLAGS := -lm -lpthread -fsanitize=address 

CC := clang
CFLAGS ?= $(INC_FLAGS) $(DEFINES) -std=gnu2y -MMD -MP -O0 -g3 -Wall -Weverything -pedantic \
 -Wno-padded -Wno-switch-enum -Wno-declaration-after-statement -Wno-unsafe-buffer-usage \
 -Wno-pre-c2y-compat -Wno-pre-c23-compat -Wno-reserved-identifier -Wno-cast-qual -Wno-reserved-macro-identifier -Wno-switch-default -Wno-covered-switch-default -Wno-reserved-identifier -Wno-implicit-int-conversion -Wno-unused-macros -Wno-sign-conversion -Wno-comma -Wno-documentation -Wno-extra-semi-stmt \
 -fsanitize=address
 
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# c source
$(BUILD_DIR)/%.c.o: %.c
	$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	$(RM) -r $(BUILD_DIR)


-include $(DEPS)

MKDIR_P ?= mkdir -p
