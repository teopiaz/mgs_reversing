	opt	c+, at+, e+, n-
	section overlay.text

	xdef s06a_command_800CD0BC
s06a_command_800CD0BC:
	dw 0x24020007 ; 800CD0BC
	dw 0xA4820B22 ; 800CD0C0
	dw 0x2402000E ; 800CD0C4
	dw 0xA4820B24 ; 800CD0C8
	dw 0x03E00008 ; 800CD0CC
	dw 0xAC800B28 ; 800CD0D0
