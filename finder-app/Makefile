.PHONY: default
default: writer

.PHONY: all
all: writer

writer.o: writer.c
	${CROSS_COMPILE}gcc -c -o writer.o writer.c

writer: writer.o
	${CROSS_COMPILE}gcc -o writer writer.o

.PHONY: clean
clean:
	-rm -v writer.o writer
