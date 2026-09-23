# M5Stack UD-CO2S monitor

M5Stack CoreS3 SE + USB Module v1.2 + I-O DATA UD-CO2S で、CO2 濃度と温湿度を表示する PlatformIO / Arduino プロジェクトです。

## 接続

1. 電源を切り、USB Module v1.2 の **SS / INT ともに CH2 のみ ON** にします。他のチャンネルは OFF にします。
2. CoreS3 SE に USB Module v1.2 を確実に装着します。
3. Module の USB-A と UD-CO2S の Micro-B をデータ通信対応ケーブルで接続します。
4. CoreS3 SE の USB-C を PC に接続します。書き込み・ログ確認と給電に使用します。

| 信号 | CoreS3 SE GPIO |
| --- | ---: |
| SCLK | 36 |
| MISO | 35 |
| MOSI | 37 |
| SS | 1 |
| INT | 14 |

`M5.Power.setExtOutput(true)` で Module 向けの給電を有効にします。SD card は使用せず、共有 SPI bus の SD CS (GPIO 4) を HIGH に保ちます。旧 USB Module ではなく **v1.2** を対象にしています。

画面も同じ SPI bus を使用します。`SPI.begin()` は `M5.begin()` より前に実行し、画面初期化後の SPI reset を避けます。GPIO 35 は画面の D/C と USB Module の MISO を兼ねるため、描画中だけ出力にし、描画完了後は入力に戻します。

測定値は初回の有効データを即座に表示し、以降は前回の採用から60秒以上経過して最初に受信した有効データに更新します。測定値だけが変わる間は CO2・温度・湿度・測定値の Serial 出力を最大1分に1回更新します。値以外の内部状態（色・接続・受信状態・警告状態）が変わった場合は、画面を即座に更新することを原則とします。色の変化、再通知の抑制解除、正常データへの復帰では、その時点の有効な測定値も即座に採用し、その時点から次の60秒を数えます。平均化は行いません。USB 受信とデータ停止の監視は常時継続します。

画面は 8-bit の `M5Canvas` 上で完成させてから転送し、測定値や接続・警告状態が変わったときだけ更新します。画面全体を消去してから文字を描く際のちらつきを抑えます。

CO2濃度は中央に大きく表示し、`ppm` はその下に表示します。数値の桁数と画面幅に合わせて文字サイズを調整し、標準フォントの最大10倍で表示します。

温度・湿度は下部に1行ずつ、CO2濃度より小さい標準フォントの4倍サイズで表示します。切断やデータ待ちなどの状態は上部に表示します。

画面の輝度は常時 32（0〜255）です。Wi-Fi 接続や時刻による輝度の切り替えは行いません。

表示の向きは `src/config.h` の `displayRotation` を変更して、再ビルド・書き込みすると切り替わります。縦向きでも画面幅・高さに合わせて配置します。

| `displayRotation` | `1` を基準にした回転 | 画面サイズ |
| --- | --- | --- |
| `0` | 270° | 240 × 320 |
| `1` | 0° | 320 × 240 |
| `2` | 90° | 240 × 320 |
| `3` | 180° | 320 × 240 |

## ビルド・書き込み

初回は設定テンプレートをコピーし、`src/config.h` を編集してください。このファイルは Git 管理外です。

```sh
cp src/config.example.h src/config.h
```

```sh
mise install
mise run test
mise run build
mise run upload
mise run monitor
```

ポートは `mise run ports` で確認できます。明示する場合は次のように task に引数を渡します。

```sh
mise run upload --upload-port /dev/cu.usbmodem...
mise run monitor --port /dev/cu.usbmodem...
```

書き込めない場合は CoreS3 SE を download mode に入れてから再実行してください。

初回のインストール・ビルドにはネットワーク接続が必要です。

## 動作と設定

- USB CDC-ACM を 115200 / 8N1 で開き、`STA\r\n` を送って計測データの送信を開始します。
- `CO2=1234,HUM=45.6,TMP=23.4` 形式の行を受信します。USB packet をまたぐ行も処理します。Serial のこの行は補正前の値で、`Estimated:` 行と画面の温湿度には下記の補正を適用します。
- **黄色（1,500 ppm 超）から警告音**が鳴ります。色が悪化したときに、黄色なら「ピピ」の2回、赤・紫なら「ピピピ」の3回を鳴らします。1回の音は200 ms、音の間隔は100 msです。
- 同じ色が続く間や、色が改善したときは鳴らしません。青・緑では鳴りません。一度鳴った段階は、そのしきい値より 100 ppm 低い値以下になるまで再通知しません（黄色: 1400 ppm 以下、赤: 2400 ppm 以下、紫: 3400 ppm 以下）。例えば黄色で鳴った後、1500 ppm を割って再び超えても、1400 ppm 以下になっていなければ鳴りません。上の段階の通知は独立して鳴ります。
- 再通知の抑制解除は、すべての有効な受信値で判定し、解除時は1分更新を待たずに測定値と画面を更新します。初回の採用値が黄色以上なら鳴らします。切断・データ停止では通知済み状態を保持し、復帰だけでは同じ段階を再通知しません。色の変化は1分を待たずに警告判定へ反映します。
- 切断時は `Connect UD-CO2S`、初回受信前は `Waiting for data`、受信行が 10 秒途絶えると `Data timeout` と表示します。古い測定値を隠し、警告音を停止します。
- 範囲外の数値（CO2: 400〜10,000 ppm、湿度: 0〜100%、温度: −40〜70℃の範囲外）は即座に `Out of range`、形式不正や `NaN` / `Inf` は `Invalid data` と表示し、測定値を隠して警告音を停止します。これらの行が届き続ける間は `Data timeout` にせず、`STA` も再送しません。正常値に戻った後は、同じ色でも最新の測定値を即座に採用して表示を再開します。
- 接続切れ・データ停止の表示と警告音の停止は1分を待たずに反映します。復帰後は前回採用から60秒未満でも、最初の有効な測定値を即座に表示します。
- 未受信・更新停止中は 5 秒間隔で `STA` を再送します。抜き差し後も再初期化します。

しきい値・音量・音の長さと間隔は `src/config.h` で変更し、再ビルドして書き込んでください。音量は 0〜255 です。初期しきい値はアプリの設定値です。

CO2 数値の文字色は、本体の LED 合わせています。

| CO2 濃度 | 文字色 | RGB |
| --- | --- | --- |
| 1,000 ppm 以下 | 青 | `#92B6FF` |
| 1,000 ppm 超〜1,500 ppm 以下 | 緑 | `#92FFAA` |
| 1,500 ppm 超〜2,500 ppm 以下 | 黄 | `#FFFFAA` |
| 2,500 ppm 超〜3,500 ppm 以下 | 赤（ピンク寄り） | `#FF92AA` |
| 3,500 ppm 超 | 紫 | `#DB92FF` |

未受信・更新停止中の `----` は灰色です。

## 温湿度の補正

自己発熱による温度上昇と相対湿度の低下を見込んで、画面には次の推定値を表示します。CO2 濃度および警告判定にこの補正は適用しません。センサーへの校正コマンドも送りません。

```text
T1 = T0 - temperatureOffset
E(T) = 6.1078 × 10^(7.5 × T / (T + 237.3))
H1 = H0 × E(T0) / E(T1)
```

`T0` / `H0` は受信した温度（℃）/ 相対湿度（%RH）、`E(T)` は飽和水蒸気圧（hPa）です。一定の水蒸気分圧を仮定して換算しています。飽和水蒸気圧と、体積あたりの水蒸気量である絶対湿度は異なる量です。

`src/config.h` の `temperatureOffset` の初期値は **4.5℃** です。例えば受信値 30.0℃ / 50.0%RH は 25.5℃ / 約 65.0%RH になります。`0.0f` にすると温湿度とも無補正になります。湿度の計算結果は 0〜100%RH に制限します。100% を超える計算結果は補正量・仮定が適切でない可能性もあり、正確な飽和状態を確認したことにはなりません。

根拠と確認できた範囲:

- [UD-CO2Sの温湿度補正の実測報告](https://blog.mono0x.net/2023/09/03/ud-co2s-temperature-and-humidity/) は、−4.5℃と飽和水蒸気圧比による補正で他の温湿度計に近づいた報告です。湿度は温度に応じて換算します。
- [Sensirion の自己発熱に関する資料](https://sensirion.com/media/documents/0FEA2450/61652EF9/Sensirion_CO2_Sensors_SCD30_Low_Power_Mode.pdf) は、自己発熱が温度・湿度に影響し、補正量は筐体や消費電力などに依存すると説明しています。
- [Sensirion の湿度資料 §2.1](https://sensirion.com/en/media/documents/8AB2AD38/61642ADD/Sensirion_AppNotes_Humidity_Sensors_Introduction_to_Relative_Humidit.pdf) の一定圧力・結露なしでの換算式と、上記の飽和水蒸気圧比の考え方は整合します。同資料の Magnus 近似と記事の Tetens 近似では係数は異なります。
- **4.5℃を全個体・全環境で使えるというメーカー保証や、公式アプリの実装そのものは確認できていません。** 初期値は経験的な補正量です。十分に温度が安定した状態で、熱源から離した基準温度計と比較し、必要なら調整してください。Serial には比較用に補正前後の値を残します。

## 確認

解析・温湿度補正・警告判定は PC 上でテストできます（C++ compiler が必要です）。

```sh
mise run test
```

温湿度補正の実測精度は未確認です。書き込み後は次を確認してください。

1. Serial に `USB host: ready` → `CDC init: 0x00` → `Sensor connected` → `STA: 0x00` と表示され、測定値が続くこと。
2. 画面の測定値が更新され、黄色への悪化時に2回、赤・紫への悪化時に3回鳴ること。同じ色が続く間や改善時は鳴らないこと。音の確認時だけ `config.h` の色のしきい値を順序を保って下げても確認できます。確認後は元に戻します。
3. UD-CO2S を抜くと値が消え、再接続すると測定が再開すること。

`USB host init failed` は DIP switch、Module の装着、給電を確認してから再起動してください。`Connect UD-CO2S` のままならケーブルやセンサーの電源を確認します。`Waiting for data` / `Data timeout` が続く場合は Serial の `STA` / `USB receive` のエラーコードを確認してください。

## 参照資料

- [M5Stack USB Module v1.2](https://docs.m5stack.com/en/module/USB%20v1.2%20Module)
- [USB Host Shield の CoreS3 対応と CH2 設定](https://github.com/felis/USB_Host_Shield_2.0/pull/843) — この変更を含む revision を `platformio.ini` で固定しています。
- [PlatformIO CoreS3 board](https://docs.platformio.org/en/latest/boards/espressif32/m5stack-cores3.html)
- [M5Unified](https://github.com/m5stack/M5Unified)
- [UD-CO2S 製品仕様](https://www.iodata.jp/product/tsushin/iot/ud-co2s/spec.htm)
- [UD-CO2S 通信の公開実装](https://gist.github.com/oquno/d07f6dbf8cc760f2534d9914efe79801) — CDC-ACM と通信形式はこの実装を根拠にしています。メーカーの通信仕様書は製品ページの申請フォーム経由であり、取得していません。実機との適合性は確認が必要です。
