all: tfortune tfortune.6.gz README

tfortune: tfortune.c
	gcc -Os -s -Wextra -Wall *.c -o tfortune

tfortune.6.gz: tfortune.xml
	docbook2x-man tfortune.xml
	groff -man tfortune.6 > tfortune.ps
	gzip -f -9 tfortune.6

README: tfortune.6.gz Makefile
	man ./tfortune.6.gz | col -b | \
		awk 'BEGIN {paraline=1}; \
		     /^[A-Z][^a-z]*$$/{paraline=0}; \
		     /^$$/{paraline++}; \
		     {if (!paraline) {print}}' \
	> README
