.PHONY: default
default: writer

.PHONY: all
all: writer

writer.o: writer.c
	$(CC) -c -o writer.o writer.c

writer: writer.o
	$(CC) -o writer writer.o

.PHONY: clean
clean:
	-rm -v writer.o writer
