# mklbr
Make LBR file from recipe

Usage: mklbr recipefile

Where recipefile contains the name of the lbr file to create and has a line for each file to be included
Lines have the format

source?name?createtime?modifytime

where
source is the path to the source file to include, or in the case of the first line the name of the lbr file to be created
name   is optional and is the name of the file in the library. If omitted the file name part of source is used. The name is stored
       in upper case
createtime - optional timestamp for the createtime entry in the lbr header
modifytime - optional timestamp for the changetime entry in the lbr header

times if omitted take the information from the source file, except for the lbr file which will use the latest source timestamp
if specified times are either
-      time info is set to 0
yyyy-mm-dd hh:mm:ss - time is set to the specified time

trailing but not intermediate question marks can be omitted, alwo whitespace around the question marks is ignored. 
Note for unix, it does mean source paths including ? will not work.

blank lines or lines beginning with space or # are ignored, so can be used for comments

Mark
