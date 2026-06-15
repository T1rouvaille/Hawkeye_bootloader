#include <stdio.h>
#include <stdlib.h>

/* Exact copy of calcrc() from bootloader crc16.c */
unsigned short calcrc(unsigned int addr, int count)
{
    short int crc;
    unsigned char i;
    unsigned char *ptr;
    unsigned char tmp;

    ptr = (unsigned char *)addr;
    tmp = *ptr;

    crc = 0;
    while (--count >= 0)
    {
        crc = crc ^ (short int) *ptr++ << 8;
        i = 8;
        do
        {
            if (crc & 0x8000)
                crc = crc << 1 ^ 0x1021;
            else
                crc = crc << 1;
        } while(--i);
    }
    return ((unsigned short)crc);
}

/* Standard CRC-16/XMODEM (no augment) for comparison */
unsigned short crc16_xmodem(unsigned char *data, int len)
{
    unsigned short crc = 0;
    int i, j;
    for (i = 0; i < len; i++)
    {
        crc ^= (unsigned short)data[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

int main(int argc, char *argv[])
{
    unsigned char *data;
    int len;
    FILE *f;

    if (argc < 2) {
        printf("Usage: %s <binary_file>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        printf("Cannot open %s\n", argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    data = (unsigned char *)malloc(len);
    fread(data, 1, len, f);
    fclose(f);

    printf("File: %s, size: %d (0x%X) bytes\n", argv[1], len, len);

    /* Test calcrc (using data from heap, simulating flash read) */
    unsigned short result1 = crc16_xmodem(data, len);
    printf("CRC16-XMODEM standard:  0x%04X\n", result1);

    /* Test with augmentation (append 2 zero bytes) */
    unsigned short crc_aug = crc16_xmodem(data, len);
    /* Augmentation: feed 2 more zero bytes */
    unsigned short aug1 = crc_aug ^ (0x00 << 8);
    for (int j = 0; j < 8; j++) {
        if (aug1 & 0x8000) aug1 = (aug1 << 1) ^ 0x1021;
        else aug1 = (aug1 << 1);
    }
    aug1 &= 0xFFFF;
    unsigned short aug2 = aug1 ^ (0x00 << 8);
    for (int j = 0; j < 8; j++) {
        if (aug2 & 0x8000) aug2 = (aug2 << 1) ^ 0x1021;
        else aug2 = (aug2 << 1);
    }
    aug2 &= 0xFFFF;
    printf("CRC16-XMODEM augmented: 0x%04X\n", aug2);

    printf("\nExpected bootloader calcrc(): 0x25C2\n");
    printf("srec_cat -crc16-l-e -XMODEM:    0x3FE7\n");
    printf("srec_cat --no-augment:          0xA5E2\n");

    free(data);
    return 0;
}
