	opt	c+, at+, e+, n-
	section overlay.text

	xdef d18a_wake_800C60BC
d18a_wake_800C60BC:
	dw 0x27BDFFE8 ; 800C60BC
	dw 0xAFB00010 ; 800C60C0
	dw 0x00808021 ; 800C60C4
	dw 0x3C02800B ; 800C60C8
	dw 0x8C42BA50 ; 800C60CC
	dw 0x3C030800 ; 800C60D0
	dw 0x00431024 ; 800C60D4
	dw 0x10400004 ; 800C60D8
	dw 0xAFBF0014 ; 800C60DC
	dw 0x3C02800B ; 800C60E0
	dw 0x0803183D ; 800C60E4
	dw 0x244205D0 ; 800C60E8
	dw 0x3C02800B ; 800C60EC
	dw 0x244205C0 ; 800C60F0
	dw 0xAE020040 ; 800C60F4
	dw 0x0C03175E ; 800C60F8
	dw 0x02002021 ; 800C60FC
	dw 0x0C0317A3 ; 800C6100
	dw 0x02002021 ; 800C6104
	dw 0x8FBF0014 ; 800C6108
	dw 0x3C02800B ; 800C610C
	dw 0x2448BA10 ; 800C6110
	dw 0x8A05002B ; 800C6114
	dw 0x9A050028 ; 800C6118
	dw 0x8A06002F ; 800C611C
	dw 0x9A06002C ; 800C6120
	dw 0xA9050003 ; 800C6124
	dw 0xB9050000 ; 800C6128
	dw 0xA9060007 ; 800C612C
	dw 0xB9060004 ; 800C6130
	dw 0x8FB00010 ; 800C6134
	dw 0x03E00008 ; 800C6138
	dw 0x27BD0018 ; 800C613C
