# mklbr
Make LBR file from recipe

```
Usage: mklbr recipefile

The recipe file contians one or more lines of the form
  sourcefile [?[lbrname] [?[createtime] [?[modifytime]]]]
Blank lines or lines beginning with space or # are ignored, so can be used for comments

The first recipe line is used to specify the name of the lbr file to create

Notes:
If lbrname is omitted then filename part of sourcefile is used the name will be converted to uppercase
Time information can be one of
	yyyy-mm-dd hh:mm:ss	explicitly sets the time field
	-					sets the lbr time field to 0
	missing				uses the source file timestamp
As a special case for the first line which specifies the lbr name, if time field is missing the max timestamp from the source files is used, unless all timestamps are 0, in which case the current time is used.

The use of ? as separators means that the ? cannot be part of a filename.
```

For windows a visual studio solution file is included, however you may need to retarget the project to your version of visual studio.

If you are using gcc then the utility can be compiled using

gcc -omklbr -O3 mklbr.c

Mark Ogden

9-Feb-2022
