doc: CombShape.ps QuryShape.ps shape.ps

MANTOPS=groff -man

%.ps: %.3
	$(MANTOPS) $< >$@
%.ps: %.n
	$(MANTOPS) $< >$@
