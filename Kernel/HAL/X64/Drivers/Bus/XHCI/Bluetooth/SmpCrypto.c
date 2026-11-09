/*
 * SmpCrypto.c — SMP legacy e/c1/s1
 *
 * 字节排法对表 BlueZ shared/crypto.c：p1/p2 与 smp_c1/smp_s1 同形；
 * e() 须在进 WifiAes（FIPS [0]=MSB）前对 key/plain 做 swap128，出站再 swap。
 * 缺 swap = Confirm Value Failed（对端验 Mconfirm 不过）。
 */
#include "XHCI/Bluetooth/Private.h"
#include "WifiCrypto/WifiAes.h"
static void Swap128(const UINT8 In[16], UINT8 Out[16]) {
    UINT32 Index;
    for (Index = 0; Index < 16u; Index++) {
        Out[Index] = In[15u - Index];
    }
}
static void XorBlock(UINT8 Out[16], const UINT8 A[16], const UINT8 B[16]) {
    UINT32 Index;
    for (Index = 0; Index < 16u; Index++) {
        Out[Index] = (UINT8)(A[Index] ^ B[Index]);
    }
}
int BluetoothSmpAesE(const UINT8 Key[16], const UINT8 Plain[16],
                     UINT8 Out[16]) {
    UINT8 KeyMsb[16];
    UINT8 PlainMsb[16];
    UINT8 CipherMsb[16];
    if (Key == 0 || Plain == 0 || Out == 0) {
        return -1;
    }
    Swap128(Key, KeyMsb);
    Swap128(Plain, PlainMsb);
    if (WifiAesEncrypt(KeyMsb, PlainMsb, CipherMsb) != 1) {
        return -1;
    }
    Swap128(CipherMsb, Out);
    return 0;
}
/*
 * BlueZ smp_c1：
 *   p1 = iat' || rat' || preq || pres
 *   p2 = ra || ia || padding
 *   c1 = e(k, e(k, r XOR p1) XOR p2)
 */
int BluetoothSmpC1(const UINT8 Key[16], const UINT8 Random[16],
                   const UINT8 PairingRequest[7],
                   const UINT8 PairingResponse[7], UINT8 InitiatorAddressType,
                   const UINT8 InitiatorAddress[6], UINT8 ResponderAddressType,
                   const UINT8 ResponderAddress[6], UINT8 Out[16]) {
    UINT8 P1[16];
    UINT8 P2[16];
    UINT8 Temp[16];
    UINT8 Inner[16];
    UINT32 Index;
    if (Key == 0 || Random == 0 || PairingRequest == 0 || PairingResponse == 0 ||
        InitiatorAddress == 0 || ResponderAddress == 0 || Out == 0) {
        return -1;
    }
    for (Index = 0; Index < 16u; Index++) {
        P1[Index] = 0;
        P2[Index] = 0;
    }
    P1[0] = InitiatorAddressType & 0x01u;
    P1[1] = ResponderAddressType & 0x01u;
    for (Index = 0; Index < 7u; Index++) {
        P1[2u + Index] = PairingRequest[Index];
    }
    for (Index = 0; Index < 7u; Index++) {
        P1[9u + Index] = PairingResponse[Index];
    }
    for (Index = 0; Index < 6u; Index++) {
        P2[Index] = ResponderAddress[Index];
    }
    for (Index = 0; Index < 6u; Index++) {
        P2[6u + Index] = InitiatorAddress[Index];
    }
    XorBlock(Temp, Random, P1);
    if (BluetoothSmpAesE(Key, Temp, Inner) < 0) {
        return -1;
    }
    XorBlock(Temp, Inner, P2);
    return BluetoothSmpAesE(Key, Temp, Out);
}
/*
 * BlueZ smp_s1(k,r1,r2)：out = r2[0..7] || r1[0..7]；再 e(k,out)。
 * STK = s1(TK, Srand, Mrand) → 调用时 Random1=Srand Random2=Mrand。
 */
int BluetoothSmpS1(const UINT8 Key[16], const UINT8 Random1[16],
                   const UINT8 Random2[16], UINT8 Out[16]) {
    UINT8 R[16];
    UINT32 Index;
    if (Key == 0 || Random1 == 0 || Random2 == 0 || Out == 0) {
        return -1;
    }
    for (Index = 0; Index < 8u; Index++) {
        R[Index] = Random2[Index];
    }
    for (Index = 0; Index < 8u; Index++) {
        R[8u + Index] = Random1[Index];
    }
    return BluetoothSmpAesE(Key, R, Out);
}
void BluetoothSmpFillRandom(UINT8 Out[16]) {
    UINT64 Stamp;
    UINT32 Index;
    UINT32 Mix;
    if (Out == 0) {
        return;
    }
    Stamp = ReadTsc();
    Mix = (UINT32)Stamp ^ (UINT32)(Stamp >> 32) ^ 0xA5A5A5A5u;
    for (Index = 0; Index < 16u; Index++) {
        Mix = Mix * 1664525u + 1013904223u + Index;
        Out[Index] = (UINT8)(Mix >> 16);
    }
}
