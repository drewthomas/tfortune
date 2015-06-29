all: tfortune tfortune.6.gz

tfortune: tfortune.c
	gcc -Os -s -Wextra -Wall *.c -o tfortune

tfortune.6.gz: tfortune.xml
	docbook2x-man tfortune.xml
	groff -man tfortune.6 > tfortune.ps
	gzip -f -9 tfortune.6
