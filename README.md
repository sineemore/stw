# stw

stw is simple text widget for X.

stw creates an unmanaged X window at specified position and starts reading stdio. stw buffers all readed text until it reads special `\0\n` line. After that it renders buffered text into X window and continues with stdio reading till next `\0\n` line.
