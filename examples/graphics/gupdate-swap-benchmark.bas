' Swap-only presenter benchmark: one task-start presentation plus 999 explicit
' updates produces 1,000 swap samples without drawing.
' Build with olimex_esp32_sbc_fabgl_graphics_profile and read GFXPROFILE.
GUPDATE 0
FOR i = 1 TO 999
  GUPDATE
END FOR i
