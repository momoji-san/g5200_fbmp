# fbmp

SHARP Brain PW-G5200 の Brainux 環境で、24bit BMP画像をサブディスプレイに表示するための C プログラムです。
`/dev/fb1` の framebuffer を直接操作して、240×120ピクセルの BMP画像を表示します。

### 動作環境

* SHARP Brain PW-G5200 （他のサブディスプレイ付きの機種では動作未確認）
* Brainux

このプログラムは、PW-G5200 のサブディスプレイが `/dev/fb1` として認識されている環境を前提としています。

### コンパイル

src フォルダにある fbmp.c を Brainux のSDカード等にコピーし、Brainux 上で以下のようにコンパイルします。
Windows PC でBrainux のSDカードを挿すして表示されるフォルダにコピーすると、Brainux からは、/boot/ ディレクトリに配置されるので、/boot/フォルダから、ホームディレクトリ /home/user/ 等にコピーしてコンパイルしてください。

```sh
gcc -O2 -Wall -o fbmp fbmp.c
```

### 使用方法

BMP画像を指定して実行します。

```sh
./fbmp image.bmp
```

正常に実行されると、下記が表示されます。

```text
Displayed: image.bmp
```

### 対応BMP

現在対応しているBMPは以下です。Windowsのペイント等で作成してください。
Windowsのペイントでは240x120ドットの画像を名前を付けて保存で24bitで保存すればOKです。

* 24bit BMP
* 無圧縮 BMP (`BI_RGB`)
* 240×120ピクセル
* 通常の下から上へ保存されるBMP
* 上から下へ保存されるBMP

### プログラム処理概要

SHARP Brain PW-G5200 の下部にあるサブディスプレイは、Brainux では framebuffer デバイスとして `/dev/fb1` からアクセスできます。

このプログラムでは、

```text
24bit BMP
   ↓
BMP画像を読み込み
   ↓
RGBから白黒へ変換
   ↓
32bit framebuffer形式へ変換
   ↓
/dev/fb1
   ↓
PW-G5200 サブディスプレイ
```

という処理を行います。

サブディスプレイのサイズは **240×120ピクセル**です。

### framebuffer

PW-G5200 では、サブディスプレイの framebuffer として `/dev/fb1` を使用します。

想定している表示仕様は以下の通りです。

| 項目            | 値           |
| ------------- | ----------- |
| デバイス          | `/dev/fb1`  |
| 解像度           | 240 × 120   |
| framebuffer   | 32bit       |
| 1ピクセル         | 4byte       |
| 1行のサイズ        | 960byte     |
| framebuffer全体 | 115,200byte |

プログラムでは、実際の framebuffer の情報を `ioctl()` で取得し、解像度、色深度、stride を確認しています。

```c
ioctl(fb, FBIOGET_VSCREENINFO, &vinfo);
ioctl(fb, FBIOGET_FSCREENINFO, &finfo);
```

そのため、想定している環境と異なる場合にはエラーとして終了します。

### BMPの白黒変換について

PW-G5200 のサブディスプレイは白黒表示のため、24bit BMPのRGBカラー情報を白黒に変換してから framebuffer に書き込みます。

各ピクセルについて、

```text
brightness = (R + G + B) / 3
```

を計算し、

```text
brightness >= 128
```

の場合は白、

```text
brightness < 128
```

の場合は黒

として扱います。

そのため、カラーBMPを指定した場合でも、サブディスプレイ上では白黒で表示されます。

### BMPの読み込みについて

BMPファイルのヘッダを読み込み、

* BMPファイルであること
* ヘッダ形式
* 画像サイズ
* 24bitであること
* 無圧縮であること

を確認します。

240×120以外の画像を指定した場合はエラーになります。

```text
Error: BMP must be 240x120 pixels
```

また、24bit以外のBMPにも対応していません。

```text
Error: BMP must be 24bit
```

### BMPの上下方向

BMPでは一般的に画像データが下の行から保存されています。

このプログラムでは `biHeight` の値を確認して、

```c
bottom_up = (info_header.biHeight > 0);
```

として、表示時に行の順番を変換しています。

そのため、通常のBMPをそのまま指定しても上下が逆にならないようになっています。

### framebufferへの書き込み

BMP画像を読み込んだ後、240×120の各ピクセルを32bit framebuffer形式へ変換します。

白の場合：

```text
FF FF FF FF
```

黒の場合：

```text
00 00 00 FF
```

としてframebuffer上に配置します。

また、framebufferの1行のサイズについては固定値を使用せず、

```c
finfo.line_length
```

を使用しています。

ピクセル位置は、

```c
pos = y * finfo.line_length + x * 4;
```

として計算しています。

### 表示更新

framebufferへ画像を書き込んだ後、

```c
ioctl(fb, FBIOPAN_DISPLAY, &vinfo);
```

を実行して表示更新を通知します。

ドライバによっては `FBIOPAN_DISPLAY` が不要、または対応していない場合があるため、この ioctl が失敗しても警告として処理し、`write()` による表示を継続します。

```text
Warning: FBIOPAN_DISPLAY failed: ...
```

### エラー処理

以下のような条件を確認しています。

* BMPファイルを開けない
* BMPヘッダを読み込めない
* BMPではない
* 対応していないBMPヘッダ
* 画像サイズが240×120ではない
* 24bit BMPではない
* 圧縮BMPである
* `/dev/fb1` を開けない
* framebuffer情報を取得できない
* framebufferの解像度が240×120ではない
* framebufferが32bitではない
* framebufferのstrideが不足している
* メモリ確保に失敗する
* BMP画像データを読み込めない
* framebufferへの書き込みに失敗する

### ファイル構成

最小構成では以下の2つだけで使用できます。

```text
.
├── fbmp.c
└── README.md
```

コンパイルすると、

```text
fbmp
```

が生成されます。

### 注意事項

このプログラムは **SHARP Brain PW-G5200 の Brainux 環境**を前提としています。

特に、

```text
/dev/fb1
240x120
32bit framebuffer
```

という環境を想定しています。

他の Brain シリーズや異なる framebuffer 構成の Linux 環境では、そのまま動作しない可能性があります。

また、サブディスプレイが白黒表示であるため、カラーBMPを表示してもカラー表示にはなりません。

### その他

Brainuからは、サブディスプレイのタッチもプログラム制御出来ることを確認していますので、画像表示だけでなく、タッチ操作と組み合わせて様々なことへの応用が可能です

* GUIでの入出力
* 時計表示
* 各種ステータス表示
* キーボード入力との連携
* Brainux 上で動作する各種ツール
* USB機器と連携した操作や表示

### License

本ソフトウエアのライセンスはMITライセンスです。
