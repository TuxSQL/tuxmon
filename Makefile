CFLAGS=-MD -MP -Wall -Wextra -O2 -g -isystem$(TUXDIR)/include
LFLAGS=-Wl,--no-as-needed

src = tuxmon.c tuxapi.c
obj = $(src:.c=.o)
dep = $(obj:.o=.d)

tuxmon: $(obj)
	LD_LIBRARY_PATH=$(TUXDIR)/lib:$(LD_LIBRARY_PATH) \
		$(TUXDIR)/bin/buildclient -l "-L${TUXDIR}/lib -ltmib -lqm" -l "-lncurses" -f "$(LFLAGS)" -f "$^" -o $@

-include $(dep)

.PHONY: clean
clean:
	rm -f $(obj) tuxmon

.PHONY: cleandep
cleandep:
	rm -f $(dep)
