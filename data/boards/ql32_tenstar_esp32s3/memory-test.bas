' Report heap before, during, and after a small graphics-window allocation.
serial_probe = OPEN(2)
PRINT #serial_probe,"TENSTAR|MEM|before|";FREE(0);",";FREE(1);",";FREE(2)
canvas_channel = OPEN(4)
WINDOW #canvas_channel,0,0,80,60
PAPER #canvas_channel,1
INK #canvas_channel,7
CLS #canvas_channel
PRINT #serial_probe,"TENSTAR|MEM|window-open|";FREE(0);",";FREE(1);",";FREE(2)
CLOSEWINDOW #canvas_channel
PRINT #serial_probe,"TENSTAR|MEM|window-closed|";FREE(0);",";FREE(1);",";FREE(2)
CLOSE serial_probe
