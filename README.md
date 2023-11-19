# mklbr
Make LBR file from recipe

Note the recipe format has changed significantly and is now much simpler. The individual recipe lines can also now be used on the command line.

```
Usage: mklbr -v | -V | [-v] (recipefile | lbrfile files+)
Where a single -v or -V shows version information
Otherwise -v provides additional information on the created lbrfile

The recipe file option makes it easier to handle multiple timestamps and CP/M file naming
However lbrfile and files are recipes and can be quoted to include more than the sourcefile
Each recipe has the following format
  sourcefile [ '|' lbrname] [modifytime] [createtime]

If lbrname is omitted then filename part of sourcefile is used
the name will be converted to uppercase
The sourcefile can be surrounded by <> to allow embedded spaces, e.g. directory path
However if there are embedded spaces in the filename part, lbrname must be specified

Time information is one of
  yyyy-mm-dd hh:mm[:ss] -- explicitly set UTC time
  -                     -- zero timestamps
  *                     -- use file timestamps (default)
If createtime is sepecified then modifytime has to be specified
Additionally createtime is set to modifytime if it is later
If this occurs when an explicit timestamp is used, a is warning issued
Note the first source file should be the name of the lbr file to create
in this case when time information is missing the max timestamps from the source files is used
The current time is used if all timestamps are set to 0 and lbr is not explicitly set
```

For windows a visual studio solution file is included, however you may need to retarget the project to your version of visual studio.

If you are using gcc then the utility can be compiled using

gcc -omklbr -O3 mklbr.c _version.c

Mark Ogden

10-Nov-2023
