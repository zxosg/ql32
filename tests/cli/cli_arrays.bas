DIM values%(2,1)
values%(0,0)=7
values%(2,1)=9
PRINT values%(0,0)
PRINT values%(2,1)
PRINT values%(1,1)
DIM names$(1)
names$(1)="QL/32"
PRINT names$(1)
DEFine FuNction ReadValue(index)
  RETurn values%(index,1)
END DEFine ReadValue
PRINT ReadValue(2)
