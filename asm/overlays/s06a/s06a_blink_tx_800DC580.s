	opt	c+, at+, e+, n-
	section overlay.text

	xdef s06a_blink_tx_800DC580
s06a_blink_tx_800DC580:
	dw 0x27BDFFE0 ; 800DC580
	dw 0xAFB10014 ; 800DC584
	dw 0x00A08821 ; 800DC588
	dw 0x24040005 ; 800DC58C
	dw 0x24050068 ; 800DC590
	dw 0xAFBF0018 ; 800DC594
	dw 0x0C005439 ; 800DC598
	dw 0xAFB00010 ; 800DC59C
	dw 0x00408021 ; 800DC5A0
	dw 0x12000016 ; 800DC5A4
	dw 0x02002021 ; 800DC5A8
	dw 0x00002821 ; 800DC5AC
	dw 0x3C06800E ; 800DC5B0
	dw 0x24C6C3CC ; 800DC5B4
	dw 0x3C07800E ; 800DC5B8
	dw 0x0C005453 ; 800DC5BC
	dw 0x24E72100 ; 800DC5C0
	dw 0x0C00825A ; 800DC5C4
	dw 0x24040070 ; 800DC5C8
	dw 0x00402021 ; 800DC5CC
	dw 0x0C037102 ; 800DC5D0
	dw 0x26050024 ; 800DC5D4
	dw 0x02002021 ; 800DC5D8
	dw 0x02202821 ; 800DC5DC
	dw 0x0C037117 ; 800DC5E0
	dw 0x00403021 ; 800DC5E4
	dw 0x04410006 ; 800DC5E8
	dw 0x02001021 ; 800DC5EC
	dw 0x0C005472 ; 800DC5F0
	dw 0x02002021 ; 800DC5F4
	dw 0x08037181 ; 800DC5F8
	dw 0x00001021 ; 800DC5FC
	dw 0x02001021 ; 800DC600
	dw 0x8FBF0018 ; 800DC604
	dw 0x8FB10014 ; 800DC608
	dw 0x8FB00010 ; 800DC60C
	dw 0x03E00008 ; 800DC610
	dw 0x27BD0020 ; 800DC614
