SRCDIR := shell
BLDDIR := $(BUILD)/obj/$(SRCDIR)

SRCS_C := $(wildcard *.c)
SRCS := $(SRCS_C)

OBJS := $(patsubst %.c,$(BLDDIR)/%.o,$(SRCS_C))

all: $(OBJS)

$(BLDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

.PHONY: all clean
