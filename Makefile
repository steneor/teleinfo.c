# Makefile pour téléinfo.c
VERSION = 1.0.0
# Indiquer quel compilateur est à utiliser
CC      ?= gcc

# Spécifier les options du compilateur
#CFLAGS  ?=-W -Wall
#CFLAGS ?=-Wall
#LDLIBS  ?=-lusb-1.0
LDLIBS  ?=-L/usr/lib/mysql -lmysqlclient

# Reconnaitre les extensions de nom de fichier *.c et *.o comme suffixe
SUFFIXES ?= .c .o
.SUFFIXES: $(SUFFIXES) .

# Nom de l'exécutable
PROG  = teleinfo

$(PROG): $(PROG).c
#gcc -o lsdevusb lsdevusb.c -W -Wall -lusb-1.0

clean:
	rm teleinfo
