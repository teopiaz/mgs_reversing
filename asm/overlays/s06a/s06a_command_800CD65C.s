	opt	c+, at+, e+, n-
	section overlay.text

	xdef s06a_command_800CD65C
s06a_command_800CD65C:
	dw 0x8C820B28 ; 800CD65C
	dw 0x94830C28 ; 800CD660
	dw 0x24420001 ; 800CD664
	dw 0xAC820B28 ; 800CD668
	dw 0x00001021 ; 800CD66C
	dw 0x03E00008 ; 800CD670
	dw 0xA4830B48 ; 800CD674
