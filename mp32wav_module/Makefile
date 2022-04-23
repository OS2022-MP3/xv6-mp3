mp32wav: main.o
	gcc -o  mp32wav main.o

main.o: minimp3.h main.c
	gcc -c main.c
	
.PHONY: clean
clean:
	rm -f mp32wav *.o