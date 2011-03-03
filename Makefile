CINCLUDES+=	`pkg-config --cflags gtk+-2.0`
LIBS+=		`pkg-config --libs gtk+-2.0`

all: squidge

squidge: squidge.c
	$(CC) $^ -o $@ $(CINCLUDES) $(LIBS) -ggdb3

clean:
	-rm *.o squidge

.PHONY: clean
