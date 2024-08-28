#ifndef _SE_DATA_BLOB_H_
#define _SE_DATA_BLOB_H_

unsigned int stepl0100[] = {
    0xD0780000, 0xD5FF0000, 0xD2290000, 0xD77F0F08,
    0xD8440000, 0xD9100000, 0x2B095A40, 0xFFFE0000
};
unsigned int stepr0100[] = {
    0xD0780000, 0xD5FF0000, 0xD22A0000, 0xD77F0F08,
    0xD8440000, 0xD9100000, 0x2B095A40, 0xFFFE0000
};
unsigned int edmg0100[] = {
    0xD0FF0000, 0xD5C00000, 0xD22B0000, 0xD77F000F,
    0xD8000000, 0xD9140000, 0x170C3C7F, 0xFFFE0000
};
unsigned int reb0100[] = {
    0xD0FF0100, 0xD57F0000, 0xD2280000, 0xD77F000F,
    0xD8000000, 0xD9140000, 0x210C5A57, 0xFFFE0000
};
unsigned int siren0500[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD2180000, 0xD77F000F,
    0xD8000000, 0xD9100000, 0x1F09501E, 0x25095032,
    0x2B095046, 0x3130145A, 0xFFFE0000
};
unsigned int siren0501[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD2170000, 0xD77F0F08,
    0xD8400000, 0xD9120000, 0x25095028, 0x2B095050,
    0x31095064, 0x37301478, 0xFFFE0000
};
unsigned int bound0200[] = {
    0xD0FF0100, 0xD5FF0000, 0xD24900FF, 0xD77F000F,
    0xD8000000, 0xD91A0000, 0x29020114, 0xD24A0000,
    0xD77F0F08, 0xD8200000, 0xD9170000, 0x18040140,
    0xD2220000, 0xD77F0F08, 0xD8200000, 0xD9170000,
    0x303C0120, 0xFFFE0000
};
unsigned int pin0100[] = {
    0xD0FF0100, 0xD57F0000, 0xD2260000, 0xDF00000F,
    0xD77F0E08, 0xD8400000, 0xD91F0000, 0x43060A7F,
    0x3703237F, 0xD2171430, 0xD77F0E08, 0xD8400000,
    0xD91F0000, 0x450C1440, 0xFFFE0000
};
unsigned int shot_e0200[] = {
    0xD0FF0100, 0xD5E00000, 0xD2400000, 0xD77F000F,
    0xD8000000, 0xD91F0000, 0x21065A50, 0xD2040000,
    0xD77F000F, 0xD8000000, 0xD91F0000, 0x34010130,
    0x21065060, 0xE4010210, 0xD9122040, 0x1A182020,
    0xE4000209, 0xFFFE0000
};
unsigned int reload0100[] = {
    0xD0FF0000, 0xD57F0000, 0xD2260000, 0xD77F000F,
    0xD8000000, 0xD91F0000, 0x2803017F, 0xD9180000,
    0x4503017F, 0x2F12017F, 0xFFFE0000
};
unsigned int c4put0100[] = {
    0xD0FF0000, 0xD5C00000, 0xD226000F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x37090154, 0x2E120154,
    0xFFFE0000
};
unsigned int c4sw0100[] = {
    0xD0FF0100, 0xD57F0000, 0xD2260000, 0xDF00000F,
    0xD77F000F, 0xD8000000, 0xD91F0000, 0x4004017F,
    0xD20D1430, 0xD77F0E08, 0xD8400000, 0xD91F0000,
    0x450C147F, 0xFFFE0000
};
unsigned int dopen0100[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD2040000, 0xD75C000F,
    0xD8000000, 0xD9140000, 0x1C0C147F, 0xE4000C04,
    0xD77F0F0F, 0xD8360000, 0xD9110000, 0x3C303254,
    0xE4000C02, 0xFFFE0000
};
unsigned int dclose0100[] = { // UNUSED, guessed from "dcl..."
    0xD0FF0000, 0xD5FF0000, 0xD2040000, 0xDF000F0F,
    0xD77F000F, 0x1008287F, 0xE4000804, 0x0406287F,
    0xD2262854, 0x28102868, 0xE4000604, 0xFFFE0000
};
unsigned int seunk06000[] = {
    0xD0FF0000, 0xD57F0000, 0xD204507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x47030168, 0x23030168,
    0xE7060140, 0x3B015A68, 0x3A015A68, 0xE803009D,
    0xD91F0000, 0xE7060140, 0x20015A40, 0x21015A40,
    0xE820FCD8, 0xFFFE0000
};
unsigned int se_unused05[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD203507F, 0xD752000F,
    0xD8000000, 0xD9180000, 0x1312327F, 0xE4030909,
    0xFFFE0000
};
unsigned int seunk06100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD2040000, 0xD77F000F,
    0x0C052854, 0xE4000C07, 0xD210507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0xE7060140, 0x2B015A40,
    0x2C015A30, 0xE802E000, 0xE7060140, 0x34015A20,
    0x3A025A18, 0xE808FC00, 0xFFFE0000
};
unsigned int se_unused06[] = { // UNUSED
    0xD0FF0000, 0xD57F0000, 0xD204507F, 0xD75E000F,
    0xD8000000, 0xD9180000, 0x43080154, 0x44090140,
    0x42080154, 0x3C090140, 0xFFFE0000
};
unsigned int se_unused07[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD2000000, 0xD77F000F,
    0xD8000000, 0xD9000000, 0xE3000000, 0x0C06637F,
    0x3C076330, 0xE4000607, 0xE3FF0FFF, 0xD77F0F0C,
    0xD8405A64, 0xD9115A64, 0x1530287F, 0xE4006007,
    0xFFFE0000
};
unsigned int seunk07200[] = {
    0xD0FF0100, 0xD5FF0000, 0xD24900FF, 0xD77F000F,
    0xD8000000, 0xD91A0000, 0x29020114, 0xD21000FF,
    0xD77F0F06, 0xD8000000, 0xD9180000, 0x32090A50,
    0xFFFE0000
};
unsigned int seunk07700[] = {
    0xD0FF0000, 0xD5FF0000, 0xD20000FF, 0xD753000F,
    0xD8480000, 0xD9100000, 0x3009637F, 0xE4000618,
    0x1809637F, 0xE400060C, 0xD58C01FF, 0xD22601FF,
    0xD77F000F, 0xD8400000, 0xD9100000, 0xE7010120,
    0x40016404, 0xE80A0400, 0xE7010120, 0x40016430,
    0xE830FFFD, 0xF2020000, 0xFFFE0000
};
unsigned int seunk06400[] = {
    0xD0800000, 0xD5FF0000, 0xD204507F, 0xD7570F07,
    0xD8000000, 0xD9180000, 0xE7060140, 0x40055A2C,
    0x45045A40, 0xE804F0D0, 0xFFFE0000
};
unsigned int ninja0100[] = {
    0xD0FF0000, 0xD5C00000, 0xD202637F, 0xD7520808,
    0xD8480C0C, 0xD911637F, 0x46302840, 0xE4000645,
    0xFFFE0000
};
unsigned int seunk07900[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD207507F,
    0xD77F000F, 0xD8000000, 0xD9180000, 0xDD000018,
    0xDF0C3218, 0x43062814, 0xFFFE0000
};
unsigned int ninja0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xD203637F, 0xD75A0808,
    0xD8480C0C, 0xD911637F, 0x47182828, 0xE4000645,
    0xFFFE0000
};
unsigned int hiza0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD204507F, 0xD760000F,
    0xD8000000, 0xD9180000, 0x04030A7F, 0x090C0A7F,
    0xFFFE0000
};
unsigned int seunk08200[] = {
    0xD0FF0000, 0xD5FF0000, 0xD24000FF, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x1F093C48, 0xD20400FF,
    0xD75C000F, 0xD8000000, 0xD9180000, 0x2606632C,
    0xE400060C, 0xFFFE0000
};
unsigned int siren0600[] = {
    0xD0FF0000, 0xD5FF0000, 0xF6020000, 0xD2020000,
    0xD77F0F0F, 0xD8000000, 0xD9000000, 0x402A0450,
    0xE4010C46, 0xE00A0A3C, 0x402E0458, 0xE4010C46,
    0x40270450, 0xE4010C46, 0xFFFE0000
};
unsigned int seunk08300[] = {
    0xD0FF0000, 0xD5FF0000, 0xF6170000, 0xD2170000,
    0xD77F0F0F, 0xD8000000, 0xD9140000, 0x1301640A,
    0x1903641E, 0x1F036432, 0x25036446, 0x2B03645A,
    0x3103646E, 0xD77F0F03, 0xD840FF10, 0x34243C7F,
    0xFFFE0000
};
unsigned int seunk08301[] = {
    0xD0FF0000, 0xD5FF0000, 0xF6170000, 0xD2170000,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x1903640A,
    0x1F03641E, 0x25036432, 0x2B036446, 0x3103645A,
    0x3703646E, 0xD77F0F03, 0xD848FF10, 0xE128FF10,
    0x3A481E7F, 0xFFFE0000
};
unsigned int seunk08302[] = {
    0xD0FF0000, 0xD5FF0000, 0xF6170000, 0xD2170000,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x1602640A,
    0x1C03641E, 0x22036432, 0x28036446, 0x2E03645A,
    0x3403646E, 0xD77F000F, 0xD8480F0F, 0xE128FF20,
    0x37481E7F, 0xFFFE0000
};
unsigned int seunk01600[] = {
    0xD0FF0000, 0xD5FF0000, 0xF60E507F, 0xD20E507F,
    0xD77F080C, 0xD8000000, 0xD9110000, 0xE7065040,
    0x3E045020, 0x42035018, 0xE8040000, 0xE7065040,
    0x3E045009, 0x42035006, 0xE8020000, 0xE7065040,
    0x3E045020, 0x42035018, 0xE8040000, 0xE7065040,
    0x3E045009, 0x42035006, 0xE8030000, 0xFFFE0000
};
unsigned int seunk01700[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD2410000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD91F0000,
    0x181A6364, 0xFFFE0000
};
unsigned int seunk04800[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD2400000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD9130000,
    0x18181E64, 0xFFFE0000
};
unsigned int idisp0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF713507F, 0xD213507F,
    0xD758080C, 0xD8400000, 0xD9120000, 0x37180A7F,
    0xE404083E, 0x37180A30, 0xE404083E, 0x37180A18,
    0xE404083E, 0xFFFE0000
};
unsigned int iget0100[] = {
    0xD0FF0000, 0xF6FF0000, 0xD5FF0000, 0xD213507F,
    0xD7550802, 0xD8000000, 0xD9110000, 0x26066470,
    0xE400061A, 0x1A262870, 0xE400203E, 0xFFFE0000
};
unsigned int iget0101[] = {
    0xD0FF0000, 0xF7FF0000, 0xD5FF0000, 0xD241507F,
    0xD77F000F, 0xD8000000, 0xD9110000, 0x24066360,
    0xD242507F, 0xD77F000F, 0xD8000000, 0xD9110000,
    0x181C6360, 0xD213507F, 0xD7550802, 0xD8000000,
    0xD9110000, 0x26066420, 0xE400061A, 0x1A262820,
    0xE400203E, 0xFFFE0000
};
unsigned int seunk02300[] = {
    0xD0FF0000, 0xD5FF0000, 0xF713507F, 0xD213507F,
    0xD77F0905, 0xD8000000, 0xD9110000, 0x3912017F,
    0xFFFE0000
};
unsigned int seunk07600[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD23F0000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD91F0000,
    0x1D2C6350, 0xE4061811, 0xFFFE0000
};
unsigned int seunk02400[] = {
    0xD0800000, 0xD5FF0000, 0xF738507F, 0xD238507F,
    0xD77F000F, 0xD8000000, 0xD9120000, 0x17126368,
    0xE4000C15, 0xFFFE0000
};
unsigned int seunk02500[] = {
    0xD0800000, 0xD5FF0000, 0xD239507F, 0xD77F000F,
    0xD8000000, 0xD9100000, 0x18185058, 0xE4000C15,
    0xFFFE0000
};
unsigned int pout000100[] = {
    0xD0800000, 0xD5FF0000, 0xF63D507F, 0xD23D507F,
    0xD77F000F, 0xD8000000, 0xD9110000, 0x16283C78,
    0x16283C40, 0x16283C20, 0x16283C10, 0x16283C08,
    0xFFFE0000
};
unsigned int rifle0100[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD2270000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD91F0000,
    0x1B326350, 0xE403180F, 0xFFFE0000
};
unsigned int shot_0100[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD2270000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD91F0000,
    0x1D206350, 0xFFFE0000
};
unsigned int sneeze0100[] = {
    0xD0800000, 0xD5DF0000, 0xF73C507F, 0xD23C507F,
    0xD77F000F, 0xD8000000, 0xD9100000, 0x18285048,
    0xFFFE0000
};
unsigned int step0200[] = {
    0xD0FF0100, 0xD5A80000, 0xF7040000, 0xD24A0000,
    0xD77F000F, 0xD8002854, 0xD9182854, 0x1C045A30,
    0xD22A507F, 0xD77F000F, 0xD8000000, 0xD9180000,
    0x2B065A40, 0xD229507F, 0xD77F000F, 0xD8000000,
    0xD9180000, 0x2B0C5A40, 0xFFFE0000
};
unsigned int wstep0100[] = {
    0xD0800000, 0xD5FF0000, 0xF6800000, 0xD2430000,
    0xD77F0F08, 0xD840647F, 0xD90F647F, 0x1A18507F,
    0xFFFE0000
};
unsigned int cur0100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD20A0130,
    0xD7620F06, 0xD8441430, 0xD9181430, 0x3C0C5068,
    0xFFFE0000
};
unsigned int start0100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD2050130,
    0xD74E0C08, 0xD8451430, 0xD9121430, 0xDD000430,
    0xDF061430, 0x47601428, 0xE400201B, 0xFFFE0000
};
unsigned int start0101[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD20A0130,
    0xD74E0C08, 0xD8451430, 0xD9111430, 0xDD000050,
    0x2D600A64, 0xFFFE0000
};
unsigned int start0102[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD2370130,
    0xD74E0C08, 0xD8451430, 0xD9111430, 0xDD00FC20,
    0x39600D3C, 0xFFFE0000
};
unsigned int win0100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD2370130,
    0xD7520E04, 0xD8441430, 0xD9101430, 0xDD00FC30,
    0x37603C23, 0xFFFE0000
};
unsigned int win0101[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6060118, 0xD2260130,
    0xD7500F03, 0xD8441430, 0xD9161430, 0xDD000440,
    0x43600A20, 0xFFFE0000
};
unsigned int idec0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF613507F, 0xD213507F,
    0xD7560808, 0xD8400000, 0xD9120000, 0x37180A7F,
    0xE404083E, 0x37180A30, 0xE404083E, 0x37180A18,
    0xE404083E, 0xFFFE0000
};
unsigned int idec0201[] = {
    0xD0FF0000, 0xD5FF0000, 0xF716507F, 0xD216507F,
    0xD7560806, 0xD8400000, 0xD9120000, 0x2B180A20,
    0xE4040832, 0xFFFE0000
};
unsigned int idec0300[] = {
    0xD0800000, 0xD5FF0000, 0xD242507F, 0xD77F000F,
    0xD8400000, 0xD9120000, 0x1C050A40, 0x26180A40,
    0xFFFE0000
};
unsigned int dclose0300[] = {
    0xD0FF0100, 0xD5FF0000, 0xD2250000, 0xD77F000F,
    0xD8480000, 0xD9140000, 0x18060118, 0xD2220000,
    0xD77F000F, 0xD8480000, 0xD9130000, 0x0C180130,
    0xFFFE0000
};
unsigned int dclose0400[] = {
    0xD0FF0100, 0xD5FF0000, 0xD2250000, 0xD77F000F,
    0xD8480000, 0xD9140000, 0x18060120, 0xD2240000,
    0xD77F000F, 0xD8480000, 0xD9130000, 0x0C180120,
    0xFFFE0000
};
unsigned int dclose0500[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD2320000,
    0xD760000F, 0xD8400000, 0xD9100000, 0x0C070A30,
    0x1060037F, 0xFFFE0000
};
unsigned int dopen0300[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD21E0000,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x15060A40,
    0xD2240000, 0xD77F000F, 0xD8400000, 0xD9110000,
    0x13401430, 0xFFFE0000
};
unsigned int dopen0400[] = {
    0xD0FF0000, 0xD5FF0000, 0xF7260000, 0xD24B0000,
    0xD7500F0D, 0xD8400000, 0xD9100000, 0x284C0A30,
    0xE4000329, 0xFFFE0000
};
unsigned int dopen0500[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD2320000,
    0xD74E000F, 0xD8400000, 0xD9100000, 0x11903C7F,
    0xFFFE0000
};
unsigned int r_sel0100[] = {
    0xD0FF0130, 0xD5FF0130, 0xF6160130, 0xD2130130,
    0xD77F000F, 0xD8001430, 0xD91C1430, 0x390C5030,
    0xFFFE0000
};
unsigned int r_snd0100[] = {
    0xD0FF0000, 0xD5A80000, 0xF60E507F, 0xD202507F,
    0xD77F080C, 0xD8000000, 0xD9110000, 0xE7065040,
    0x32045050, 0x36035040, 0xE8040000, 0xE7065040,
    0x32045012, 0x3603500C, 0xE8020000, 0xE7065040,
    0x32045050, 0x36035040, 0xE8040000, 0xE7065040,
    0x32045012, 0x3603500C, 0xE8030000, 0xFFFE0000
};
unsigned int karasht100[] = {
    0xD0800100, 0xD5FF0000, 0xF7000000, 0xD2420130,
    0xD77F000F, 0xD8001430, 0xD9181430, 0x260C0140,
    0xFFFE0000
};
unsigned int ataru0100[] = {
    0xD0FF0000, 0xD5C00000, 0xD23F0100, 0xD77F000F,
    0xD8000000, 0xD9000000, 0x24036330, 0xE4000707,
    0xD2470FFF, 0xD77F0F0C, 0xD8405A64, 0xD9115A64,
    0x15062860, 0x1A182860, 0xFFFE0000
};
unsigned int rebdrm0100[] = {
    0xD0FF0100, 0xD5A00000, 0xF7260000, 0xD2260000,
    0xD77F000F, 0xD8000000, 0xD9130000, 0x34015A60,
    0x28015A60, 0x34015A60, 0x28015A60, 0xD2280000,
    0xD77F000F, 0xD8000000, 0xD9130000, 0x28185A40,
    0xFFFE0000
};
unsigned int shot_e0300[] = {
    0xD0FF0100, 0xD5B00000, 0xD2400000, 0xD77F000F,
    0xD8000000, 0xD91F0000, 0x1B095A7F, 0xD77F0F01,
    0xD9132040, 0x13205060, 0xFFFE0000
};
unsigned int exp_0500[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7040000, 0xD23F0000,
    0xD77F000F, 0xD8000000, 0xD9000000, 0x1106637F,
    0x1806637F, 0xD23F0000, 0xDF10000F, 0xD77F000F,
    0xD8000000, 0xD9000000, 0x0C07647F, 0xE4000705,
    0xD77F0F0C, 0xD8405A64, 0xD9105A64, 0x0890467F,
    0xE4009000, 0xFFFE0000
};
unsigned int wall0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xD210507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x26030140, 0xE4000328,
    0xFFFE0000
};
unsigned int wall0300[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xD210507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x26030140, 0xE4000328,
    0xD210507F, 0xD77F000F, 0xD8000000, 0xD9140000,
    0x150C0A40, 0xE4000C21, 0x150C0A20, 0xE4000C21,
    0x150C0A10, 0xE4000C21, 0xFFFE0000
};
unsigned int buzzer0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD20D507F, 0xD77F0F08,
    0xD8000000, 0xD91F0000, 0x280C1E30, 0x281C6330,
    0xFFFE0000
};
unsigned int hibana0100[] = {
    0xD0FF0000, 0xD5600000, 0xD204507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x47030168, 0x23030168,
    0xE7060140, 0x3B015A60, 0x3A015A60, 0xE803009D,
    0x47030158, 0x23030158, 0xE7060140, 0x3B015A50,
    0x3A015A50, 0xE803009D, 0xFFFE0000
};
unsigned int camera0300[] = {
    0xD0FF0000, 0xD5C00000, 0xD241507F, 0xD77F000F,
    0xD8000000, 0xD9180000, 0x3C01647F, 0x240E507F,
    0xFFFE0000
};
unsigned int bakuha0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD23F507F,
    0xD77F000F, 0xD8000000, 0xD90F0000, 0x15056360,
    0x09056332, 0x0405637F, 0x10FF6360, 0xFFFE0000
};
unsigned int bakuha0101[] = {
    0xD0FF0100, 0xD5FF0000, 0xF707507F, 0xD204507F,
    0xD77F000F, 0xD800507F, 0xD914507F, 0x1805017F,
    0x0C050150, 0xD204507F, 0xD77F0008, 0xD87F0000,
    0xD9100000, 0x013F637F, 0xD77F0004, 0x03C02830,
    0xFFFE0000
};
unsigned int button0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD242507F,
    0xD77F000F, 0xD8000000, 0xD9190000, 0x24090140,
    0x2B0C0120, 0xFFFE0000
};
unsigned int elecls0300[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD21E0000,
    0xD77F000F, 0xD8480000, 0xD9140000, 0x18060130,
    0xD2320000, 0xD77F000F, 0xD8420000, 0xD9130000,
    0x0C303C60, 0xFFFE0000
};
unsigned int eleopn0300[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD2320000,
    0xD74A000F, 0xD8400000, 0xD9100000, 0x17905A7F,
    0xFFFE0000
};
unsigned int hasigo_l00[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD229507F,
    0xD75E000F, 0xD8000000, 0xD9120000, 0x2D0A0148,
    0xFFFE0000
};
unsigned int hasigo_r00[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD22A507F,
    0xD75E000F, 0xD8000000, 0xD9120000, 0x2E0A0148,
    0xFFFE0000
};
unsigned int inelev0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xE3FF00FF,
    0xD226507F, 0xD74C0608, 0xD8000000, 0xD9100000,
    0xE5004006, 0x070C6430, 0xE70C6446, 0xF30C6446,
    0xE8000000, 0xF3600100, 0xE4006002, 0xFFFE0000
};
unsigned int inelev0201[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD232507F,
    0xD750000F, 0xD8000000, 0xD90A0000, 0xE500070C,
    0x12103260, 0xD753000F, 0xE5000100, 0x18203C40,
    0xD219507F, 0xD742000F, 0xD8000000, 0xD9110000,
    0x390C6403, 0xE7C06446, 0xF30C6446, 0xE8000000,
    0xF3180100, 0xE4001835, 0xD232507F, 0xD750000F,
    0xD8000000, 0xD9100000, 0xE5000803, 0x12153260,
    0x18303C40, 0xFFFE0000
};
unsigned int inelev0202[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD24B507F,
    0xD742000F, 0xD8000000, 0xD9110000, 0x1D0C6410,
    0xE7605A32, 0xF30C6432, 0xE8000000, 0xF318017F,
    0xD203507F, 0xD74A0808, 0xD83C0000, 0xD9100000,
    0x2B603218, 0xE4006010, 0xFFFE0000
};
unsigned int moustep200[] = {
    0xD0FF0000, 0xD5C80000, 0xF625507F, 0xD225507F,
    0xD760000F, 0xD8000000, 0xD91B0000, 0x39030120,
    0xD9170000, 0x3A05017F, 0xD91A0000, 0x38030120,
    0xD9160000, 0x39060178, 0xD91B0000, 0x37030120,
    0xD9160000, 0x3804016F, 0xD91A0000, 0x38030120,
    0xD9170000, 0x39060166, 0xFFFE0000
};
unsigned int senaka0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF704507F, 0xD229507F,
    0xD760000F, 0xD8000000, 0xD90F0000, 0x2D06637F,
    0xD22A507F, 0xD760000F, 0x2803637F, 0xD227507F,
    0xD74F0A0F, 0xD8000000, 0xD91E0000, 0x350C5A40,
    0xD7440A0F, 0x37125A50, 0xFFFE0000
};
unsigned int siren300[] = { // UNUSED
    0xD09B0000, 0xD5B40000, 0xF607507F, 0xD20D507F,
    0xD75C000F, 0xD8360000, 0xD9100000, 0xE106FF10,
    0xE5000604, 0x22301434, 0x210E630C, 0xFFFE0000
};
unsigned int stand0100[] = {
    0xD0FF0000, 0xD5A80000, 0xF707507F, 0xD204507F,
    0xD75D000F, 0xD8000000, 0xD9150000, 0x23060A40,
    0x200C0A40, 0xD229507F, 0xD75D000F, 0xD8000000,
    0xD90F0000, 0x2D0C6340, 0xD75D000F, 0x28086320,
    0xFFFE0000
};
unsigned int kamae0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF7420000, 0xD2420000,
    0xD77F000F, 0xD8000000, 0xD91F0000, 0x1F040160,
    0xD9180000, 0x24040160, 0x21205040, 0xFFFE0000
};
unsigned int heart0100[] = {
    0xD0800000, 0xD5C00000, 0xF6800000, 0xD2440000,
    0xD77F000F, 0xD8466350, 0xD9106350, 0xDD00F250,
    0x1C0E3C40, 0xDD000E50, 0x1A303C10, 0xFFFE0000
};
unsigned int rebglass00[] = {
    0xD0FF0000, 0xD5800000, 0xF607507F, 0xD23F507F,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x39035A40,
    0xE4000321, 0xD246507F, 0xD77F000F, 0xD8000000,
    0xD9140000, 0x24065A40, 0x260C5A40, 0xFFFE0000
};
unsigned int glass1100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD245507F, 0xD77F000F,
    0xD8000000, 0xD9130000, 0x1F245A50, 0xE4081022,
    0xFFFE0000
};
unsigned int p_swing100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD248507F, 0xD757000F,
    0xD8000000, 0xD9190000, 0xE5001208, 0x29185A50,
    0xFFFE0000
};
unsigned int k_swing100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD248507F, 0xD757000F,
    0xD8000000, 0xD9190000, 0xE5001208, 0x21185A50,
    0xFFFE0000
};
unsigned int kickhit100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD23F507F, 0xD77F0F07,
    0xD8440000, 0xD9190000, 0x1C035040, 0xD247507F,
    0xD77F0F0A, 0xD8000000, 0xD9120000, 0x18200A60,
    0xFFFE0000
};
unsigned int punchit100[] = {
    0xD0FF0000, 0xD5FF0000, 0xD247507F, 0xD77F0F0A,
    0xD8000000, 0xD9120000, 0x1A205050, 0xE4061815,
    0xFFFE0000
};
unsigned int sight0800[] = {
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD209507F,
    0xD7550A08, 0xD84A0000, 0xD90F0000, 0xE100FF0F,
    0x3C303214, 0xFFFE0000
};
unsigned int r_windw100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF604507F, 0xD213507F,
    0xD7560C06, 0xD8480000, 0xD9110000, 0xE5001224,
    0x32240A20, 0xFFFE0000
};
unsigned int r_windw101[] = {
    0xD0FF0000, 0xD5FF0000, 0xF604507F, 0xD208507F,
    0xD7540C06, 0xD8480000, 0xD9110000, 0xE5001218,
    0x45240A0C, 0xFFFE0000
};
unsigned int r_windw200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF605507F, 0xD204507F,
    0xD7580F0A, 0xD8000000, 0xD91C0000, 0x2D105A18,
    0xD207507F, 0xD7560C06, 0xD8480000, 0xD9110000,
    0xE50012F4, 0x39240A10, 0xFFFE0000
};
unsigned int r_windw201[] = {
    0xD0FF0000, 0xD5FF0000, 0xF705507F, 0xD20A507F,
    0xD77F000F, 0xD8000000, 0xD91C0000, 0x2F0C5A12,
    0xF613507F, 0xD213507F, 0xD7560C08, 0xD8480000,
    0xD9110000, 0xE50012F4, 0x34240A20, 0xFFFE0000
};
unsigned int r_tune0100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6000000, 0xD2130130,
    0xD77F000F, 0xD8001430, 0xD91C1430, 0x3C060130,
    0xFFFE0000
};
unsigned int r_disp01[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xF61B507F, 0xD203507F,
    0xD755000F, 0xD8000000, 0xD9110000, 0xE5002418,
    0x45245030, 0xFFFE0000
};
unsigned int r_cancel00[] = {
    0xD0FF0100, 0xD5A00000, 0xF6000000, 0xD20A0130,
    0xD77F000F, 0xD8001430, 0xD91A1430, 0x3C0C1430,
    0xFFFE0000
};
unsigned int r_cursor00[] = {
    0xD0FF0100, 0xD5A00000, 0xF6000000, 0xD2130130,
    0xD77F000F, 0xD84D1430, 0xD91C1430, 0x39035030,
    0xD2160130, 0xD77F000F, 0xD8001430, 0xD91A1430,
    0x390C0A12, 0x390C0A04, 0xFFFE0000
};
unsigned int zoom0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF70E507F, 0xD213507F,
    0xD77F000F, 0xD8000000, 0xD9160000, 0xE7065040,
    0x3C010118, 0x3C010118, 0xE8080000, 0xF3080140,
    0xE4000837, 0xFFFE0000
};
unsigned int zoom0101[] = {
    0xD0FF0000, 0xD5FF0000, 0xF70E507F, 0xD217507F,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x46183C07,
    0xE4100835, 0xFFFE0000
};
unsigned int full000500[] = {
    0xD0FF0000, 0xD5C00000, 0xF613507F, 0xD20A507F,
    0xD77F0F08, 0xD8480000, 0xD9140000, 0x2B06017F,
    0xE4030339, 0x37060140, 0xE4030339, 0x37060130,
    0xE4030339, 0x37060120, 0xE4030339, 0x37060118,
    0xE4030339, 0x3706010C, 0xE4030339, 0x37060106,
    0xE4030339, 0xFFFE0000
};
unsigned int item000300[] = {
    0xD0FF0000, 0xD5400000, 0xF613507F, 0xD211507F,
    0xD760000F, 0xD8000000, 0xD9120000, 0x2F060140,
    0x36060160, 0x3B06017F, 0x42060130, 0x47060140,
    0x42060118, 0x47060120, 0xFFFE0000
};
unsigned int kaihuku100[] = {
    0xD0FF0000, 0xD5A80000, 0xF613507F, 0xDF08507F,
    0xD20E507F, 0xD758000F, 0xD8360000, 0xD9160000,
    0xE5010C18, 0x3B0C5060, 0x3E0B5050, 0x410A5040,
    0x440A5030, 0x47095020, 0xFFFE0000
};
unsigned int over0300[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD226507F,
    0xD77F0C08, 0xD83A0000, 0xD90F0000, 0xDD00007F,
    0x3CFF647F, 0xE400FF30, 0xF3803C7F, 0xE400FF2A,
    0xFFFE0000
};
unsigned int over0301[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD219507F,
    0xD77F0C08, 0xD83A0000, 0xD90F0000, 0xDD00007F,
    0x45FF6440, 0xF3803C00, 0xFFFE0000
};
unsigned int over0302[] = {
    0xD0FF0000, 0xD5800000, 0xF607507F, 0xD203507F,
    0xD77F0F0A, 0xD8480000, 0xD9160000, 0xD640FF40,
    0xDF0BEC40, 0xED00EC40, 0xDD00EC40, 0x3B04017F,
    0xDD001440, 0x38040170, 0xDD00EC40, 0x36040160,
    0xDD001440, 0x39040170, 0xDD00EC40, 0x3B04017F,
    0xDD001440, 0x38040170, 0xDD00EC40, 0x36040160,
    0xDD001440, 0x39040170, 0xEE00EC40, 0xDF0AEC40,
    0xEE00EC40, 0xDF09EC40, 0xEE00EC40, 0xD6F40040,
    0xDF08EC40, 0xEE00EC40, 0xDF07EC40, 0xEE00EC40,
    0xDF06EC40, 0xEE00EC40, 0xDF05EC40, 0xEE00EC40,
    0xDF04EC40, 0xEE00EC40, 0xDF03EC40, 0xEE00EC40,
    0xDF02EC40, 0xEE00EC40, 0xDF01EC40, 0xEE00EC40,
    0xFFFE0000
};
unsigned int piyo000100[] = { // UNUSED
    0xD0FF0000, 0xD5800000, 0xF707507F, 0xDF0C507F,
    0xD209507F, 0xD7600C0F, 0xD8440000, 0xD9120000,
    0xED00017F, 0xE000017F, 0x3B040130, 0xE00C2860,
    0x3C0C2820, 0xE0060150, 0x3B040118, 0xE0122860,
    0x3C0C2818, 0xE0062830, 0x3C0C2810, 0xEE04F400,
    0xD5700740, 0xEE040740, 0xD5600740, 0xEE040740,
    0xD5400740, 0xEE040740, 0xFFFE0000
};
unsigned int kaidan2l00[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD229507F,
    0xD77F0F08, 0xD8000000, 0xD91F0000, 0x32020140,
    0xD229507F, 0xD75E000F, 0xD8000000, 0xD9120000,
    0x2B0A0140, 0xFFFE0000
};
unsigned int kaidan2r00[] = { // UNUSED
    0xD0FF0000, 0xD5FF0000, 0xF707507F, 0xD229507F,
    0xD77F0F08, 0xD8000000, 0xD91F0000, 0x33020140,
    0xD22A507F, 0xD75E000F, 0xD8000000, 0xD9120000,
    0x2D0A0140, 0xFFFE0000
};
unsigned int down000200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF732507F, 0xD247507F,
    0xD758000F, 0xD8000000, 0xD9130000, 0x21035050,
    0x15065040, 0xD240507F, 0xD77F000F, 0xD8000000,
    0xD9130000, 0x14182830, 0xFFFE0000
};
unsigned int start00100[] = {
    0xD0800000, 0xD5FF0000, 0xF6800000, 0xD2270000,
    0xD77F0F08, 0xD8420000, 0xD9110000, 0x1A12507F,
    0x1A125060, 0x1A125030, 0x1A125018, 0x1A12500C,
    0x1A125006, 0xFFFE0000
};
unsigned int start00101[] = {
    0xD0800000, 0xD5FF0000, 0xF6800000, 0xD2270000,
    0xD77F000F, 0xD8420000, 0xD9110000, 0x24105060,
    0xD2490000, 0xD77F0F08, 0xD8420000, 0xD9110000,
    0x2D0C507F, 0x2D0A5020, 0x2D185010, 0xFFFE0000
};
unsigned int o2damage00[] = {
    0xD0800000, 0xD5FF0000, 0xF6800000, 0xD2160000,
    0xD77F0F08, 0xD8420000, 0xD9110000, 0xDD00F120,
    0x47060410, 0x440C040C, 0xFFFE0000
};
unsigned int radar00100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF613507F, 0xD216507F,
    0xD77F000F, 0xD8360000, 0xD9180000, 0xDD000F7F,
    0x3C091E10, 0xD77F0F0B, 0xD8420000, 0xD9120000,
    0x3C405018, 0xFFFE0000
};
unsigned int pout000200[] = {
    0xD0C00100, 0xD5C00000, 0xF6390130, 0xD23A0130,
    0xD754000F, 0xD8001430, 0xD9121430, 0xE5001805,
    0x1730507F, 0x17305040, 0x17305020, 0x17305018,
    0x1730500C, 0xFFFE0000
};
unsigned int r_noise100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF705507F, 0xD20A507F,
    0xD77F000F, 0xD83E0000, 0xD91C0000, 0xDD000018,
    0x2F0F4612, 0x2F23460C, 0xFFFE0000
};
unsigned int r_noise101[] = {
    0xD0FF0000, 0xD5FF0000, 0xF605507F, 0xD204507F,
    0xD77F0F0A, 0xD8000000, 0xD91C0000, 0xDD000020,
    0x3901640A, 0x2D016410, 0x32016418, 0xE7016420,
    0xED016410, 0x39016420, 0x2D016420, 0x32016420,
    0xEE05FFF8, 0xE80400F8, 0xE7000000, 0xEE000000,
    0xE805FEF8, 0xFFFE0000
};
unsigned int camera0600[] = {
    0xD0FF0000, 0xD5A80000, 0xF700507F, 0xD213507F,
    0xD77F000F, 0xD8000000, 0xD9140000, 0xE7066430,
    0xE0006418, 0x2A016418, 0xE0106410, 0x29016412,
    0xE8080000, 0xE7066430, 0xE0006418, 0x2A016418,
    0xE0106410, 0x29016412, 0xE808FEF8, 0x2A020100,
    0xFFFE0000
};
unsigned int shattr0400[] = {
    0xD0AA507F, 0xD5C8507F, 0xF745507F, 0xD232000F,
    0xD77F000F, 0xD8000000, 0xD9110000, 0x1108506E,
    0xD754000F, 0x1D40506E, 0xE418181C, 0xFFFE0000
};
unsigned int shattr0401[] = {
    0xD0AA507F, 0xD5C8507F, 0xF745507F, 0xD23F000F,
    0xD77F000F, 0xD8000000, 0xD9120000, 0xF2040A50,
    0x0C480A50, 0xFFFE0000
};
unsigned int shattr0600[] = {
    0xD0800000, 0xD5AA0000, 0xF704507F, 0xD24A507F,
    0xD764000F, 0xD8000000, 0xD9120000, 0x30063218,
    0x2F063216, 0x30063218, 0x2F073216, 0xFFFE0000
};
unsigned int shattr0601[] = {
    0xD0800000, 0xD5AA0000, 0xF704507F, 0xD24B507F,
    0xD768000F, 0xD8000000, 0xD9120000, 0x1C303230,
    0xFFFE0000
};
unsigned int idec0400[] = {
    0xD0800000, 0xD5FF0000, 0xF642507F, 0xD242507F,
    0xD77F000F, 0xD8400000, 0xD9120000, 0x1C030A40,
    0x24180A40, 0xFFFE0000
};
unsigned int el_chm0200[] = {
    0xD0FF0100, 0xD5C00000, 0xF6330000, 0xD2110000,
    0xD77F0F06, 0xD8400000, 0xD9100000, 0x34400A50,
    0xFFFE0000
};
unsigned int elstop0600[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xF218507F,
    0xD232507F, 0xD750000F, 0xD8000000, 0xD9100000,
    0xE5000803, 0x12153240, 0x18303C28, 0xFFFE0000
};
unsigned int elstop0601[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD203507F,
    0xD74A0F08, 0xD83C0000, 0xD9100000, 0x1F601E40,
    0xE4005E0C, 0xFFFE0000
};
unsigned int hohuku0100[] = { // UNUSED
    0xD0FF0100, 0xD5A00000, 0xD204507F, 0xD75B000F,
    0xD8000000, 0xD9150000, 0x23060A30, 0x280C1420,
    0xD2450000, 0xD75D0808, 0xD8400000, 0xD9100000,
    0x18061E20, 0x18061E10, 0xFFFE0000
};
unsigned int hohuku0200[] = { // UNUSED
    0xD0FF0100, 0xD5A00000, 0xD204507F, 0xD75B000F,
    0xD8000000, 0xD9150000, 0x23060A30, 0x1F0C1420,
    0xD2450000, 0xD75D0808, 0xD8400000, 0xD9100000,
    0x18061E20, 0x18061E10, 0xFFFE0000
};
unsigned int ration0100[] = {
    0xD0FF0000, 0xD5FF0000, 0xF642507F, 0xD245507F,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x24090A30,
    0xD201507F, 0xD77F0F0A, 0xD8440000, 0xD9130000,
    0x30120A40, 0xFFFE0000
};
unsigned int mask000200[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6040000, 0xD24C0000,
    0xD7500F0F, 0xD8000000, 0xD9110000, 0x1C482830,
    0xFFFE0000
};
unsigned int chaf000200[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7040000, 0xD23F0000,
    0xD77F000F, 0xD8000000, 0xD9130000, 0x10093260,
    0xD77F000F, 0xD8005A64, 0xD9105A64, 0x24485050,
    0xE4002C00, 0xFFFE0000
};
unsigned int chaf000300[] = {
    0xD0FF0100, 0xD5780000, 0xF6040000, 0xF2030000,
    0xE0100000, 0xD2130000, 0xD77F0F08, 0xD8000000,
    0xD9120000, 0xDD00F630, 0x39050130, 0x36050138,
    0x38050140, 0x35050148, 0x3B050150, 0x38050158,
    0x3A050160, 0x37050158, 0x3A050150, 0x37050148,
    0x39050140, 0x36050148, 0x3C050150, 0x39050158,
    0x3B050160, 0x38050158, 0x3B050150, 0x38050148,
    0x3A050140, 0x37050138, 0x3D050130, 0x3A050128,
    0x3C050120, 0x39050118, 0x3B050110, 0x3805010C,
    0x3A050108, 0x37050105, 0xFFFE0000
};
unsigned int chaf000301[] = {
    0xD0FF0100, 0xD5780000, 0xF6040000, 0xD2130000,
    0xD77F0F08, 0xD8000000, 0xD9120000, 0xDD000A30,
    0x3A050130, 0x37050138, 0x39050140, 0x36050148,
    0x3C050150, 0x39050158, 0x3B050160, 0x38050158,
    0x39050150, 0x36050148, 0x38050140, 0x35050148,
    0x3B050150, 0x38050158, 0x3A050160, 0x37050158,
    0x3B050150, 0x38050148, 0x3A050140, 0x37050138,
    0x3D050130, 0x3A050128, 0x3C050120, 0x39050118,
    0x3B050110, 0x3805010C, 0x3A050108, 0x37050105,
    0xFFFE0000
};
unsigned int pout000300[] = {
    0xD0C00100, 0xD5DF0000, 0xF6390130, 0xD23A0130,
    0xD77F000F, 0xD8001430, 0xD9121430, 0xE50B06FD,
    0xDD000060, 0x132A5070, 0x132A5038, 0x132A501C,
    0x132A500E, 0xFFFE0000
};
unsigned int camera0700[] = {
    0xD0FF0000, 0xD5800000, 0xF7066430, 0xD24B507F,
    0xD768000F, 0xD8000000, 0xD9130000, 0xDFF03210,
    0x45303210, 0xE4181842, 0xFFFE0000
};
unsigned int jingle0100[] = {
    0xD0540000, 0xD5FF0000, 0xF6185A0A, 0xD2185A0A,
    0xD77F0F05, 0xD8005A0A, 0xD90E5A0A, 0xDD00000C,
    0x33045A60, 0x39045A70, 0x3F045A7F, 0x450C5A70,
    0x33045A0C, 0x39045A0E, 0x3F045A10, 0x45245A0E,
    0xFFFE0000
};
unsigned int jingle0101[] = {
    0xD0540000, 0xD5FF0000, 0xF60C5A0A, 0xF20C5A0A,
    0xD2185A0A, 0xD77F0F05, 0xD8005A0A, 0xD90E5A0A,
    0xDD00000C, 0x33045A18, 0x39045A1C, 0x3F045A20,
    0x450C5A1C, 0x33045A06, 0x39045A07, 0x3F045A08,
    0x450C5A07, 0xFFFE0000
};
unsigned int jingle0200[] = {
    0xD0400000, 0xD5FF0000, 0xF6175A0A, 0xD2365A0A,
    0xD744060A, 0xD83C5A0A, 0xD9105A0A, 0xDD00FA0C,
    0x3FA84620, 0xFFFE0000
};
unsigned int jingle0201[] = {
    0xD0400000, 0xD5FF0000, 0xF6365A0A, 0xD2365A0A,
    0xD744060A, 0xD83C5A0A, 0xD9105A0A, 0xDD00060C,
    0x3CA84620, 0xFFFE0000
};
unsigned int jingle0202[] = {
    0xD0400000, 0xD5FF0000, 0xF6365A0A, 0xD2175A0A,
    0xD744060A, 0xD83C5A0A, 0xD9105A0A, 0xDD00000C,
    0x39A84610, 0xFFFE0000
};
unsigned int jingle0300[] = { // UNUSED
    0xD0400000, 0xD5FF0000, 0xF6175A0A, 0xD2365A0A,
    0xD77F0806, 0xD8385A0A, 0xD90F5A0A, 0xDD00000C,
    0x39903C50, 0xFFFE0000
};
unsigned int jingle0301[] = { // UNUSED
    0xD0400000, 0xD5FF0000, 0xF6175A0A, 0xD2365A0A,
    0xD77F0806, 0xD8385A0A, 0xD90F5A0A, 0xDD00000C,
    0x38903C50, 0xFFFE0000
};
unsigned int jingle0302[] = { // UNUSED
    0xD0400000, 0xD5FF0000, 0xF6175A0A, 0xF2180A40,
    0xD2355A0A, 0xD754080A, 0xD8385A0A, 0xD90F5A0A,
    0xDD00000C, 0x22183C40, 0x21483230, 0xFFFE0000
};
unsigned int signal0400[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD206507F,
    0xD77F0F06, 0xD8000000, 0xD9120000, 0xDD000018,
    0x46182860, 0xFFFE0000
};
unsigned int mmask00100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6390130, 0xDD01F660,
    0xD2040130, 0xD758000F, 0xD8001430, 0xD9121430,
    0x300C0A60, 0x240C0A60, 0x18180560, 0xD2480130,
    0xD752000F, 0xD8001430, 0xD9121430, 0x1D095028,
    0x1A095028, 0x1C203C20, 0xE4002010, 0xFFFE0000
};
unsigned int clock00100[] = {
    0xD0FF0000, 0xD5C80000, 0xF6800000, 0xD2420000,
    0xD7600F08, 0xD844647F, 0xD91C647F, 0xDD00007F,
    0x37180A7F, 0x2B180A20, 0x34180A40, 0x2B180A20,
    0xFFFE0000
};
unsigned int signal0200[] = {
    0xD0FF0000, 0xD5FF0000, 0xF607507F, 0xD207507F,
    0xD77F000F, 0xD8000000, 0xD9180000, 0xDD000018,
    0x47062818, 0x470C2818, 0xFFFE0000
};
unsigned int radar00300[] = {
    0xD0FF0000, 0xD57F0000, 0xF604507F, 0xD204507F,
    0xDD000F7F, 0xD77F000F, 0xD8001430, 0xD91C1430,
    0x240C5014, 0xD754000F, 0xD8001430, 0xD91C1430,
    0x3030630E, 0xFFFE0000
};
unsigned int radar00301[] = {
    0xD0FF0000, 0xD57F0000, 0xF604507F, 0xD20A507F,
    0xDD000F7F, 0xD754000F, 0xD8001430, 0xD91C1430,
    0x30406318, 0xE40C0637, 0xFFFE0000
};
unsigned int hohuku0300[] = {
    0xD0FF0100, 0xD5A00000, 0xD204507F, 0xD759000F,
    0xD8000000, 0xD9140000, 0x17060A40, 0x1C0C1430,
    0x100C3C18, 0xFFFE0000
};
unsigned int hohuku0400[] = {
    0xD0FF0100, 0xD5A00000, 0xD204507F, 0xD759000F,
    0xD8000000, 0xD9140000, 0x1C060A40, 0x170C1430,
    0x0B0C3C18, 0xFFFE0000
};
unsigned int kaihuku400[] = {
    0xD0FC0000, 0xD5C00000, 0xF613507F, 0xD20E507F,
    0xD7540605, 0xD8400000, 0xD9120000, 0x2F202830,
    0xE4002047, 0xFFFE0000
};
unsigned int kaihuku401[] = {
    0xD0FC0100, 0xD5780000, 0xF613507F, 0xD237507F,
    0xD77F000F, 0xD8400000, 0xD9120000, 0x30302810,
    0xFFFE0000
};
unsigned int kaihuku402[] = {
    0xD0FC0100, 0xD5780000, 0xF613507F, 0xD213507F,
    0xD77F000F, 0xD8400000, 0xD9120000, 0x3030280C,
    0xFFFE0000
};
unsigned int shot_m0200[] = {
    0xD0780000, 0xF7FF0000, 0xD5FF0000, 0xD2270000,
    0xDF00000F, 0xD77F000F, 0xD8000000, 0xD91F0000,
    0xE5000303, 0x1E206350, 0xE4041C1B, 0xFFFE0000
};
unsigned int facechg100[] = {
    0xD0FF0000, 0xD5600000, 0xF604507F, 0xD204507F,
    0xDD00F67F, 0xD77F000F, 0xD8001430, 0xD91C1430,
    0x30035018, 0x24095018, 0xE4000C47, 0xFFFE0000
};
unsigned int facechg101[] = {
    0xD0FF0000, 0xD5600000, 0xF604507F, 0xD20A507F,
    0xDD00F67F, 0xD77F0F08, 0xD8401430, 0xD9121430,
    0x30186340, 0xE4001846, 0xFFFE0000
};
unsigned int shattr0b00[] = {
    0xD080507F, 0xD5C8507F, 0xF204000F, 0xD227000F,
    0xD77F000F, 0xD83F0000, 0xD9110000, 0xE50004FA,
    0x10302820, 0xFFFE0000
};
unsigned int shattr0b01[] = {
    0xD080507F, 0xD5C8507F, 0xD232000F, 0xD754000F,
    0xD8460000, 0xD9110000, 0x1030637F, 0xFFFE0000
};
unsigned int menuopn100[] = {
    0xD0FC0000, 0xD5FF0000, 0xF604507F, 0xDD01007F,
    0xD203507F, 0xD77F000F, 0xD8001430, 0xD91F1430,
    0x241A6320, 0xE4001A39, 0xD226507F, 0xD77F0905,
    0xD8471430, 0xD9121430, 0x3C145060, 0x3C145028,
    0x3C1B3C14, 0xD203507F, 0xD75C000F, 0xD8001430,
    0xD9121430, 0x30302810, 0xE4003000, 0xFF008D00,
    0xFFFE0000
};
unsigned int menuopn101[] = {
    0xD0FC0000, 0xD5FF0000, 0xF604507F, 0xDD01007F,
    0xF2286318, 0xD219507F, 0xD74E000F, 0xD8001430,
    0xD9121430, 0x2148320C, 0xD237000F, 0xD77F000F,
    0xD8001430, 0xD9121430, 0x39302808, 0xFFFE0000
};
unsigned int run0000100[] = {
    0xD0FC0000, 0xD5C80000, 0xF6920000, 0xD24D0100,
    0xD748000F, 0xD8000000, 0xD9110000, 0x16186428,
    0xE4001818, 0xE7C05A64, 0x180C642C, 0xE8000000,
    0xF3601464, 0xFFFE0000
};
unsigned int chr_dsp100[] = {
    0xD0FC0000, 0xD5FF0000, 0xF607507F, 0xD2190F0C,
    0xD7440308, 0xD8400000, 0xD9110000, 0x24FF4628,
    0xFFFE0000
};
unsigned int chr_dsp101[] = {
    0xD0FC0000, 0xD5FF0000, 0xF607507F, 0xD2190F0C,
    0xD7440308, 0xD8400000, 0xD9110000, 0x22FF4614,
    0xFFFE0000
};
unsigned int chr_dsp102[] = {
    0xD0FC0000, 0xD5FF0000, 0xF607507F, 0xD2360F0C,
    0xD7440308, 0xD8400000, 0xD9110000, 0x2BFF4614,
    0xFFFE0000
};
unsigned int isel000300[] = {
    0xD0FF0000, 0xD5C00000, 0xF613507F, 0xD213507F,
    0xD77F0905, 0xD8000000, 0xD9110000, 0xDD000E60,
    0x39120160, 0xFFFE0000
};
unsigned int backcls100[] = {
    0xD0FF0100, 0xD5FF0000, 0xF6260000, 0xD21E0000,
    0xD77F000F, 0xD8000000, 0xD9140000, 0x15060A40,
    0xD2240000, 0xD77F000F, 0xD8400000, 0xD9110000,
    0x13241430, 0xD2250000, 0xD77F000F, 0xD8480000,
    0xD9140000, 0x18060118, 0xD2220000, 0xD77F000F,
    0xD8480000, 0xD9130000, 0x0C180130, 0xFFFE0000
};
unsigned int backcls200[] = {
    0xD0FF0100, 0xD5FF0000, 0xF7260000, 0xD2320000,
    0xD74E000F, 0xD8400000, 0xD9100000, 0x11405A7F,
    0xD2320000, 0xD760000F, 0xD8400000, 0xD9100000,
    0x1060037F, 0xFFFE0000
};
unsigned int seunk10100[] = {
#ifdef VR_EXE
    0xD0FC0100, 0xD5960000, 0xF6800000, 0xD2270100,
    0xD77F000F, 0xD8006366, 0xD9196366, 0x2201014E,
    0xD24E0100, 0xD77F000F, 0xD8006366, 0xD9126366,
    0xE00E6356, 0x220A6362, 0xD762000F, 0x240F632C,
    0xFFFE0000
#else
    0xD0FC0100, 0xD5960000, 0xF6800000, 0xD2270100,
    0xD77F000F, 0xD8006366, 0xD9196366, 0x22010146,
    0xD24E0100, 0xD77F000F, 0xD8006366, 0xD9126366,
    0xE00E6356, 0x220A635A, 0xD762000F, 0x240F6324,
    0xFFFE0000
#endif
};

#endif // _SE_DATA_BLOB_H_
