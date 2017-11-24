#!/bin/bash
# A simple shell script to sgftopng list of sgfs
for file in KGSinput/*.sgf
   do
     echo "Value of sgf is: $file" &
     ./sgftopng KGSoutput/$file.png  < $file;
done
