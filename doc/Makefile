doc: doc-pdf

doc-ps: CombShape.ps QuryShape.ps shape.ps

doc-pdf: CombShape.pdf QuryShape.pdf shape.pdf

doc-dvi: CombShape.dvi QuryShape.dvi shape.dvi

doc-txt: CombShape.txt QuryShape.txt shape.txt

doc-html: CombShape.html QuryShape.html shape.html

MANTOPS=groff -man

%.ps: %.3
	$(MANTOPS) -Tps $< >$@
%.ps: %.n
	$(MANTOPS) -Tps $< >$@
%.pdf: %.3
	$(MANTOPS) -Tpdf $< >$@
%.pdf: %.n
	$(MANTOPS) -Tpdf $< >$@
%.dvi: %.3
	$(MANTOPS) -Tdvi $< >$@
%.dvi: %.n
	$(MANTOPS) -Tdvi $< >$@
%.txt: %.3
	$(MANTOPS) -Tutf8 $< >$@
%.txt: %.n
	$(MANTOPS) -Tutf8 $< >$@
%.html: %.3
	$(MANTOPS) -Thtml $< >$@
%.html: %.n
	$(MANTOPS) -Thtml $< >$@

.PHONY: doc doc-ps doc-pdf doc-dvi doc-txt doc-html
