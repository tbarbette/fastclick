#include <stdbool.h>
#include "swifsymbol.h"

uint8_t gf256_mul(uint8_t a, uint8_t b, uint8_t *mul) { 
    return mul[a * 256 + b];
}


uint8_t gf256_mul_formula(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    for (int i = 0 ; i < 8 ; i++) {
        if ((b % 2) == 1) p ^= a;
        b >>= 1;
        bool carry = (a & 0x80) != 0;
        a <<= 1;
        if (carry) {
            a ^= 0x1d;
        }
    }
    return p;
}


/**
 * @brief Take a symbol and add another symbol multiplied by a 
 *        coefficient, e.g. performs the equivalent of: p1 += coef * p2
 * @param[in,out] p1     First symbol (to which coef*p2 will be added)
 * @param[in]     coef  Coefficient by which the second packet is multiplied
 * @param[in]     p2     Second symbol
 */
void symbol_add_scaled
(void *symbol1, uint8_t coef, const void *symbol2, uint32_t symbol_size, uint8_t *mul)
{
    uint8_t *data1 = (uint8_t *) symbol1;
    uint8_t *data2 = (uint8_t *) symbol2; 
    for (uint32_t i=0; i<symbol_size; i++) {
        data1[i] ^= gf256_mul(coef, data2[i], mul);
    }
}

bool symbol_is_zero(void *symbol, uint32_t symbol_size) {
    uint8_t *data8 = (uint8_t *) symbol;
    uint64_t *data64 = (uint64_t *) symbol;
    for (int i = 0 ; i < symbol_size/8 ; i++) {
        if (data64[i] != 0) return false;
    }
    for (int i = (symbol_size/8)*8 ; i < symbol_size ; i++) {
        if (data8[i] != 0) return false;
    }
    return true;
}

void symbol_mul(uint8_t *symbol1, uint8_t coef, uint32_t symbol_size, uint8_t *mul) {
    for (uint32_t i=0; i<symbol_size; i++) {
        symbol1[i] = gf256_mul(coef, symbol1[i], mul);
    }
}

void assign_inv(uint8_t *array) {
        array[0] = 0; array[1] = 1; array[2] = 142; array[3] = 244; array[4] = 71; array[5] = 167; array[6] = 122; array[7] = 186; array[8] = 173; array[9] = 157; array[10] = 221; array[11] = 152; array[12] = 61; array[13] = 170; array[14] = 93; array[15] = 150; array[16] = 216; array[17] = 114; array[18] = 192; array[19] = 88; array[20] = 224; array[21] = 62; array[22] = 76; array[23] = 102; array[24] = 144; array[25] = 222; array[26] = 85; array[27] = 128; array[28] = 160; array[29] = 131; array[30] = 75; array[31] = 42; array[32] = 108; array[33] = 237; array[34] = 57; array[35] = 81; array[36] = 96; array[37] = 86; array[38] = 44; array[39] = 138; array[40] = 112; array[41] = 208; array[42] = 31; array[43] = 74; array[44] = 38; array[45] = 139; array[46] = 51; array[47] = 110; array[48] = 72; array[49] = 137; array[50] = 111; array[51] = 46; array[52] = 164; array[53] = 195; array[54] = 64; array[55] = 94; array[56] = 80; array[57] = 34; array[58] = 207; array[59] = 169; array[60] = 171; array[61] = 12; array[62] = 21; array[63] = 225; array[64] = 54; array[65] = 95; array[66] = 248; array[67] = 213; array[68] = 146; array[69] = 78; array[70] = 166; array[71] = 4; array[72] = 48; array[73] = 136; array[74] = 43; array[75] = 30; array[76] = 22; array[77] = 103; array[78] = 69; array[79] = 147; array[80] = 56; array[81] = 35; array[82] = 104; array[83] = 140; array[84] = 129; array[85] = 26; array[86] = 37; array[87] = 97; array[88] = 19; array[89] = 193; array[90] = 203; array[91] = 99; array[92] = 151; array[93] = 14; array[94] = 55; array[95] = 65; array[96] = 36; array[97] = 87; array[98] = 202; array[99] = 91; array[100] = 185; array[101] = 196; array[102] = 23; array[103] = 77; array[104] = 82; array[105] = 141; array[106] = 239; array[107] = 179; array[108] = 32; array[109] = 236; array[110] = 47; array[111] = 50; array[112] = 40; array[113] = 209; array[114] = 17; array[115] = 217; array[116] = 233; array[117] = 251; array[118] = 218; array[119] = 121; array[120] = 219; array[121] = 119; array[122] = 6; array[123] = 187; array[124] = 132; array[125] = 205; array[126] = 254; array[127] = 252; array[128] = 27; array[129] = 84; array[130] = 161; array[131] = 29; array[132] = 124; array[133] = 204; array[134] = 228; array[135] = 176; array[136] = 73; array[137] = 49; array[138] = 39; array[139] = 45; array[140] = 83; array[141] = 105; array[142] = 2; array[143] = 245; array[144] = 24; array[145] = 223; array[146] = 68; array[147] = 79; array[148] = 155; array[149] = 188; array[150] = 15; array[151] = 92; array[152] = 11; array[153] = 220; array[154] = 189; array[155] = 148; array[156] = 172; array[157] = 9; array[158] = 199; array[159] = 162; array[160] = 28; array[161] = 130; array[162] = 159; array[163] = 198; array[164] = 52; array[165] = 194; array[166] = 70; array[167] = 5; array[168] = 206; array[169] = 59; array[170] = 13; array[171] = 60; array[172] = 156; array[173] = 8; array[174] = 190; array[175] = 183; array[176] = 135; array[177] = 229; array[178] = 238; array[179] = 107; array[180] = 235; array[181] = 242; array[182] = 191; array[183] = 175; array[184] = 197; array[185] = 100; array[186] = 7; array[187] = 123; array[188] = 149; array[189] = 154; array[190] = 174; array[191] = 182; array[192] = 18; array[193] = 89; array[194] = 165; array[195] = 53; array[196] = 101; array[197] = 184; array[198] = 163; array[199] = 158; array[200] = 210; array[201] = 247; array[202] = 98; array[203] = 90; array[204] = 133; array[205] = 125; array[206] = 168; array[207] = 58; array[208] = 41; array[209] = 113; array[210] = 200; array[211] = 246; array[212] = 249; array[213] = 67; array[214] = 215; array[215] = 214; array[216] = 16; array[217] = 115; array[218] = 118; array[219] = 120; array[220] = 153; array[221] = 10; array[222] = 25; array[223] = 145; array[224] = 20; array[225] = 63; array[226] = 230; array[227] = 240; array[228] = 134; array[229] = 177; array[230] = 226; array[231] = 241; array[232] = 250; array[233] = 116; array[234] = 243; array[235] = 180; array[236] = 109; array[237] = 33; array[238] = 178; array[239] = 106; array[240] = 227; array[241] = 231; array[242] = 181; array[243] = 234; array[244] = 3; array[245] = 143; array[246] = 211; array[247] = 201; array[248] = 66; array[249] = 212; array[250] = 232; array[251] = 117; array[252] = 127; array[253] = 255; array[254] = 126; array[255] = 253;
}