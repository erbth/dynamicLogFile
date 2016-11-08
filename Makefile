BUILDDIR=build
# CFLAGS=-gdwarf-2
CFLAGS=-O3

.PHONY: all
all: ${BUILDDIR} ${BUILDDIR}/dynamicLogFile

${BUILDDIR}/dynamicLogFile: dynamicLogFile.c
	gcc ${CFLAGS} -o $@ $<

${BUILDDIR}:
	mkdir ${BUILDDIR}

.PHONY: clean
clean:
	rm -rf ${BUILDDIR}
