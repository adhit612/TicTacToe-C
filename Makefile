ttts: ttts.o 
	gcc -o ttts -fsanitize=address -g -pthread ttts.o

ttts.o: ttts.c
	gcc -c -fsanitize=address -g ttts.c

clean:
	rm -f ttts *.o