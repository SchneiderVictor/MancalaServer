PORT = 50000

server: mancsrv
	./mancsrv -p ${PORT}

client:
	nc 127.0.0.1 ${PORT}

mancsrv: mancsrv.c
	gcc -Wall -std=gnu99 -g -o mancsrv mancsrv.c
