CC = gcc -Wall

all: rdpr rdps

rdpr: rdpr.o
	$(CC) -o rdpr rdpr.o

rdps: rdps.o
	$(CC) -o rdps rdps.o

clean:
	rm -f rdpr rdpr.o rdps rdps.o

