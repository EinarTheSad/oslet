SRCDIR := arch
BLDDIR := $(BUILD)/$(SRCDIR)

SRCS_C := $(wildcard *.c)
SRCS_S := $(wildcard *.S)
SRCS := $(SRCS_C) $(SRCS_S)

OBJS := $(patsubst %.c,$(BLDDIR)/%.o,$(SRCS_C)) \
        $(patsubst %.S,$(BLDDIR)/%.o,$(SRCS_S))

all: $(OBJS)

$(BLDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BLDDIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) -m32 -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
