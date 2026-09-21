/*
 * fbmp.c
 *
 * SHARP Brain PW-G5200 / Brainux
 *
 * 240x120 / 24bit BMP を
 * /dev/fb1 に表示するプログラム
 *
 * 使い方:
 *   ./fbmp image.bmp
 *
 * 対応:
 *   - BMP 24bit
 *   - 無圧縮(BI_RGB)
 *   - 240 x 120 pixels
 *
 * BMPの下から上へ保存される形式にも対応
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <errno.h>

#define FB_DEVICE "/dev/fb1"

#define WIDTH  240
#define HEIGHT 120

/*
 * BMP File Header
 * 14 bytes
 */
#pragma pack(push, 1)

typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

/*
 * BMP Info Header
 * 40 bytes
 */
typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;

#pragma pack(pop)


/*
 * BMP 1ピクセルを読み込んで
 * 黒/白を判定する
 *
 * 戻り値:
 *   0 = 黒
 *   1 = 白
 */
static int pixel_is_white(unsigned char b,
                          unsigned char g,
                          unsigned char r)
{
    /*
     * 白黒BMPを想定。
     *
     * RGBの平均値を使って白黒判定する。
     *
     * 128以上 → 白
     * 128未満 → 黒
     */
    int brightness;

    brightness = ((int)r + (int)g + (int)b) / 3;

    return brightness >= 128;
}


int main(int argc, char *argv[])
{
    FILE *bmp;
    int fb;
    BMPFileHeader file_header;
    BMPInfoHeader info_header;

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    unsigned char *framebuffer;
    unsigned char *image;
    unsigned char *row;

    long framebuffer_size;

    int x, y;
    int bmp_row_size;
    int bottom_up;

    ssize_t written;


    /*
     * 引数チェック
     */
    if (argc != 2) {
        fprintf(stderr,
                "Usage: %s image.bmp\n",
                argv[0]);
        return 1;
    }


    /*
     * BMPを開く
     */
    bmp = fopen(argv[1], "rb");

    if (bmp == NULL) {
        perror("Cannot open BMP");
        return 1;
    }


    /*
     * BMP File Header
     */
    if (fread(&file_header,
              sizeof(file_header),
              1,
              bmp) != 1) {

        fprintf(stderr, "Cannot read BMP file header\n");
        fclose(bmp);
        return 1;
    }


    /*
     * "BM" チェック
     */
    if (file_header.bfType != 0x4D42) {
        fprintf(stderr,
                "Error: not a BMP file\n");
        fclose(bmp);
        return 1;
    }


    /*
     * BMP Info Header
     */
    if (fread(&info_header,
              sizeof(info_header),
              1,
              bmp) != 1) {

        fprintf(stderr,
                "Cannot read BMP info header\n");
        fclose(bmp);
        return 1;
    }


    /*
     * BMP形式チェック
     */
    if (info_header.biSize != 40) {
        fprintf(stderr,
                "Error: unsupported BMP header size: %u\n",
                info_header.biSize);
        fclose(bmp);
        return 1;
    }


    /*
     * サイズチェック
     */
    if (info_header.biWidth != WIDTH ||
        abs(info_header.biHeight) != HEIGHT) {

        fprintf(stderr,
                "Error: BMP must be %dx%d pixels\n"
                "       Current: %d x %d\n",
                WIDTH,
                HEIGHT,
                info_header.biWidth,
                info_header.biHeight);

        fclose(bmp);
        return 1;
    }


    /*
     * 24bit BMPのみ
     */
    if (info_header.biBitCount != 24) {
        fprintf(stderr,
                "Error: BMP must be 24bit\n"
                "       Current: %u bit\n",
                info_header.biBitCount);

        fclose(bmp);
        return 1;
    }


    /*
     * 無圧縮BMPのみ
     *
     * BI_RGB = 0
     */
    if (info_header.biCompression != 0) {
        fprintf(stderr,
                "Error: compressed BMP is not supported\n");
        fclose(bmp);
        return 1;
    }


    /*
     * BMPの1行のサイズ
     *
     * BMPでは4byte境界に
     * パディングされる。
     */
    bmp_row_size =
        ((WIDTH * 3 + 3) / 4) * 4;


    /*
     * Heightが正:
     *
     *   BMPは下から上へ保存
     *
     * Heightが負:
     *
     *   BMPは上から下へ保存
     */
    bottom_up = (info_header.biHeight > 0);


    /*
     * framebufferを開く
     */
    fb = open(FB_DEVICE, O_RDWR);

    if (fb < 0) {
        perror("Cannot open /dev/fb1");
        fclose(bmp);
        return 1;
    }


    /*
     * framebuffer情報取得
     */
    if (ioctl(fb,
              FBIOGET_VSCREENINFO,
              &vinfo) < 0) {

        perror("FBIOGET_VSCREENINFO");
        close(fb);
        fclose(bmp);
        return 1;
    }


    if (ioctl(fb,
              FBIOGET_FSCREENINFO,
              &finfo) < 0) {

        perror("FBIOGET_FSCREENINFO");
        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * PW-G5200の想定値を確認
     */
    if (vinfo.xres != WIDTH ||
        vinfo.yres != HEIGHT) {

        fprintf(stderr,
                "Error: framebuffer is not %dx%d\n"
                "       Current: %u x %u\n",
                WIDTH,
                HEIGHT,
                vinfo.xres,
                vinfo.yres);

        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * 32bit framebufferを想定
     */
    if (vinfo.bits_per_pixel != 32) {

        fprintf(stderr,
                "Error: framebuffer is not 32bpp\n"
                "       Current: %u bpp\n",
                vinfo.bits_per_pixel);

        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * strideを使用する。
     *
     * PW-G5200では
     *
     * 240 x 4 = 960 bytes
     *
     * の予定。
     */
    if (finfo.line_length < WIDTH * 4) {

        fprintf(stderr,
                "Error: framebuffer stride is too small\n");

        close(fb);
        fclose(bmp);
        return 1;
    }


    framebuffer_size =
        (long)finfo.line_length * HEIGHT;


    /*
     * framebuffer用メモリ
     */
    framebuffer =
        malloc(framebuffer_size);

    if (framebuffer == NULL) {

        fprintf(stderr,
                "Cannot allocate framebuffer memory\n");

        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * BMP画像用メモリ
     */
    image =
        malloc((size_t)bmp_row_size * HEIGHT);

    if (image == NULL) {

        fprintf(stderr,
                "Cannot allocate image memory\n");

        free(framebuffer);
        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * framebufferを白で初期化
     *
     * 32bit:
     *
     * FF FF FF FF
     *
     * とする。
     */
    for (long i = 0;
         i < framebuffer_size;
         i += 4) {

        framebuffer[i + 0] = 0xFF;
        framebuffer[i + 1] = 0xFF;
        framebuffer[i + 2] = 0xFF;
        framebuffer[i + 3] = 0xFF;
    }


    /*
     * BMP画像データへ移動
     */
    if (fseek(bmp,
              file_header.bfOffBits,
              SEEK_SET) != 0) {

        fprintf(stderr,
                "Cannot seek to BMP image data\n");

        free(image);
        free(framebuffer);
        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * BMP画像を読む
     */
    if (fread(image,
              bmp_row_size,
              HEIGHT,
              bmp) != HEIGHT) {

        fprintf(stderr,
                "Cannot read BMP image data\n");

        free(image);
        free(framebuffer);
        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * BMP → framebuffer
     */
    for (y = 0; y < HEIGHT; y++) {

        int source_y;

        if (bottom_up)
            source_y = HEIGHT - 1 - y;
        else
            source_y = y;

        row =
            image +
            (long)source_y * bmp_row_size;


        for (x = 0; x < WIDTH; x++) {

            /*
             * BMPは
             * B G R
             * の順番。
             */
            unsigned char b =
                row[x * 3 + 0];

            unsigned char g =
                row[x * 3 + 1];

            unsigned char r =
                row[x * 3 + 2];

            int white =
                pixel_is_white(b, g, r);


            /*
             * framebuffer上の位置
             */
            long pos =
                (long)y * finfo.line_length
                + (long)x * 4;


            /*
             * 白黒に変換
             */
            if (white) {

                framebuffer[pos + 0] = 0xFF;
                framebuffer[pos + 1] = 0xFF;
                framebuffer[pos + 2] = 0xFF;
                framebuffer[pos + 3] = 0xFF;

            } else {

                framebuffer[pos + 0] = 0x00;
                framebuffer[pos + 1] = 0x00;
                framebuffer[pos + 2] = 0x00;
                framebuffer[pos + 3] = 0xFF;
            }
        }
    }


    /*
     * framebufferの先頭へ
     */
    if (lseek(fb, 0, SEEK_SET) < 0) {

        perror("lseek framebuffer");

        free(image);
        free(framebuffer);
        close(fb);
        fclose(bmp);
        return 1;
    }


    /*
     * framebufferへ書き込む
     */
    written =
        write(fb,
              framebuffer,
              framebuffer_size);

    if (written < 0) {

        perror("write framebuffer");

        free(image);
        free(framebuffer);
        close(fb);
        fclose(bmp);
        return 1;
    }


    if (written != framebuffer_size) {

        fprintf(stderr,
                "Warning: only %ld / %ld bytes written\n",
                (long)written,
                framebuffer_size);
    }


    /*
     * 表示位置を更新
     */
    vinfo.xoffset = 0;
    vinfo.yoffset = 0;

    if (ioctl(fb,
              FBIOPAN_DISPLAY,
              &vinfo) < 0) {

        /*
         * ドライバによっては
         * FBIOPAN_DISPLAYが不要/未対応の場合がある。
         *
         * その場合でもwrite()で表示できる
         * ドライバがあるため、警告にする。
         */
        fprintf(stderr,
                "Warning: FBIOPAN_DISPLAY failed: %s\n",
                strerror(errno));
    }


    /*
     * 後片付け
     */
    free(image);
    free(framebuffer);

    close(fb);
    fclose(bmp);


    printf("Displayed: %s\n", argv[1]);

    return 0;
}

