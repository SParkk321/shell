all: shell

shell: shell.c
	gcc shell.c -lreadline -lhistory -ltermcap -o shell

clean:
	rm -f *.o
	rm -f shell

.PHONY: clean
