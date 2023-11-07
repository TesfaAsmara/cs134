lab3a: lab3a.c
	gcc -g -Wall -Wextra -o lab3a lab3a.c -lm
	
default:
	./lab3a trivial.img
