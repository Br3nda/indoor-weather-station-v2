# Haven't considered any sort of minifying yet.

XXD=xxd
GZIP=gzip
SED=sed

network-selector.h: network-selector.html
	${GZIP} < $< > network-selector.html.gz
	${XXD} -i network-selector.html.gz | ${SED} 's/unsigned/constexpr/' > $@

clean:
	rm -f network-selector.h network-selector.html.gz
