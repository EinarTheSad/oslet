SRCDIR := lib
BLDDIR := $(BUILD)/$(SRCDIR)

SRCS_C := $(filter-out elf.c mode_marker.c,$(wildcard *.c))
SRCS := $(SRCS_C)

OBJS := $(patsubst %.c,$(BLDDIR)/%.o,$(SRCS_C))

all: $(OBJS) $(BLDDIR)/elf.o $(BLDDIR)/mode_marker_gix.o $(BLDDIR)/mode_marker_agix.o

$(BLDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(BLDDIR)/elf.o: elf.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -c $< -o $@

$(BLDDIR)/mode_marker_gix.o: mode_marker.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DGIX_BUILD -c $< -o $@

$(BLDDIR)/mode_marker_agix.o: mode_marker.c
	@mkdir -p $(dir $@)
	$(CC) $(BINCFLAGS) -DAGIX_BUILD -c $< -o $@

clean:
	rm -f $(OBJS) $(BLDDIR)/elf.o $(BLDDIR)/mode_marker_gix.o $(BLDDIR)/mode_marker_agix.o

.PHONY: all clean
