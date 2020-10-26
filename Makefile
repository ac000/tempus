TARGETS = tempus timer

.PHONY: all $(TARGETS)
all: $(TARGETS)

MAKE_OPTS = --no-print-directory V=$V

.PHONY: tempus
tempus:
	@echo -e "Building: tempus"
	@$(MAKE) $(MAKE_OPTS) -C src/tempus

.PHONY: timer
timer:
	@echo -e "Building: timer"
	@$(MAKE) $(MAKE_OPTS) -C src/timer

.PHONY: clean
clean:
	@echo -e "Cleaning: $(TARGETS)"
	@$(MAKE) $(MAKE_OPTS) -C src/tempus clean
	@$(MAKE) $(MAKE_OPTS) -C src/timer clean
