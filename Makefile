all:
	arm-linux-gcc -O2 -static main.c ./src/*.c -I ./inc -o ./bin/main
