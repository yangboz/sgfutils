#!/bin/bash

g_src_file_path="$1"
g_dst_folder_path="$2"


if [ -z "${g_src_file_path}" ]
then
    printf "Please iput the source file path!\n"
    exit 1
fi

if [ -z "${g_dst_folder_path}" ]
then
    printf "Please input the save folder path!\n"
    exit 1
fi

if [ ! -e "${g_src_file_path}" ]
then
    printf "The source file path %s is invalid path!\n" "${g_src_file_path}"
    exit 1
fi

if [ ! -e "${g_dst_folder_path}" ]
then
    printf "The save folder path %s is invalid path!\n" "${g_dst_folder_path}"
    exit 1
fi

g_src_file=$(basename "${g_src_file_path}")
g_src_file_name="${g_src_file%.*}"
g_src_file_suffix="${g_src_file##*.}"

# printf "%s : %s : %s\n" "${g_src_file}" "${g_src_file_name}" "${g_src_file_suffix}"

if [ "${g_src_file_suffix}" != "sgf" ]
then 
    printf "The input file %s is not a sgf file \n" "${g_src_file_path}"
    exit 1
fi

g_save_file="${g_dst_folder_path}/${g_src_file_name}.png"

g_new_tool_name="/app/sgftopng_$(date +%s%N)"

# cp -f /app/sgftopng "${g_new_tool_name}"

# "${g_new_tool_name}" "${g_save_file}" < "${g_src_file_path}"
/app/sgftopng "${g_save_file}" < "${g_src_file_path}"
# sleep 3

# rm -f "${g_new_tool_name}"

printf "Finish convert to file %s\n" "${g_save_file}"

exit 0

# # A simple shell script to sgftopng list of sgfs
# for file in KGSinput/*.sgf
#    do
#      echo "Value of sgf is: $file" &
#      ./sgftopng KGSoutput/$file.png  < $file;
# done
