cd C:\PSGrooPIC\
del *.hex /f /q /s

cd C:\"Program Files (x86)"\PICC
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW301="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW310="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW315="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW341="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW301="true" +GWBOOTLOADER="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW310="true" +GWBOOTLOADER="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW315="true" +GWBOOTLOADER="true" C:\PSGrooPIC\main.c
ccsc +FH +Y9 -L -A -E -M -P -J -D +GFW341="true" +GWBOOTLOADER="true" C:\PSGrooPIC\main.c

del *.err /f /q /s
del *.esym /f /q /s
