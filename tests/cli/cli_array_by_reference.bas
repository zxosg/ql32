DIM values%(2)
WRAP_VALUE values%
PRINT values%(1)
PRINT READ_VALUE(values%)
DEFine PROCedure WRAP_VALUE(items%)
    SET_VALUE items%
END DEFine
DEFine PROCedure SET_VALUE(items%)
    items%(1) = 73
END DEFine
DEFine FuNction READ_VALUE(items%)
    RETurn items%(1)
END DEFine
