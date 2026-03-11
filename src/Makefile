BUILD ?= build
SRCDIR := .
BLDDIR := $(BUILD)

SRCS_C := $(wildcard *.c)
SRCS := $(SRCS_C)

OBJS := $(patsubst %.c,$(BLDDIR)/%.o,$(SRCS_C))

all: $(OBJS)

$(BLDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
