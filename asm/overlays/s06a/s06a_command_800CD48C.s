	opt	c+, at+, e+, n-
	section overlay.text

	xdef s06a_command_800CD48C
s06a_command_800CD48C:
	dw 0x24020002 ; 800CD48C
	dw 0xA4820B22 ; 800CD490
	dw 0x24020005 ; 800CD494
	dw 0xA4820B24 ; 800CD498
	dw 0x03E00008 ; 800CD49C
	dw 0xAC800B28 ; 800CD4A0
