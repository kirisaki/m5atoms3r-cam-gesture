# 手検出の実機評価

AtomS3R-CAM上でESPDet-Picoの手検出を評価する、独立したESP-IDFプロジェクトです。
既存のArduino版ジェスチャ判定・HID・I2Cはこのファームには含みません。
接続済みの受信ディスプレイはNO SIGNAL / WAITになります。

## 固定した依存関係

- ESP-IDF **5.5.1**（現時点のビルド検証対象）
- `espressif/hand_detect` **0.2.0**、`espressif/esp-dl` **3.3.13**
- `espressif/esp32-camera` **2.1.8**
- モデル：ESP32-S3向けINT8 `espdet_pico_224_224_hand.espdl`（入力224×224 RGB）

配布コンポーネントは`idf_component.yml`と`dependencies.lock`で固定します。
モデルと外部ソースはComponent Managerで取得し、このリポジトリには複製しません。
手検出コンポーネントのライセンスはMITです。外部依存にはそれぞれのライセンスが適用されます。

## ビルド・書き込み

ESP32-S3用ツールをインストール済みのESP-IDF 5.5.1環境で実行します。

```bash
# ESP-IDFのexport.shをsourceしたシェルで、リポジトリ直下から実行
cd experiments/hand-detect
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor --no-reset
```

ポートは必ずカメラ自身のものを確認します。HID版が動いている場合は先にシリアルで
`0u` を送り、HID停止・書き込みモードへ移行してください。
Windows/WSLではその後カメラをattachし直します（[USB手順](../../docs/usb.md)）。
評価版はUSB Serial/JTAGを使うため、HID版とはUSB構成が異なります。

元のHID版へ戻す際は、モニターを閉じてリポジトリ直下で
`pio run -e atoms3r-cam -t upload --upload-port /dev/ttyACM0` を実行します。
HID版は再起動後にWindows側へ戻る場合があります。

## 処理とログ

1. PSRAMにRGB画像を確保し、黒い画像でモデルのロード・推論を試験します。
   `MODEL_SMOKE` は推論が完了した確認であり、手検出の精度を証明するものではありません。
2. GC0308を320×240 RGB565で起動し、約2秒露出を安定させます。
3. RGB888へ変換し、時計回り90°に回転して推論します。これは現行の設置方向に合わせた仮定です。
   入力は240×320で、モデル前処理が縦横比を維持して224×224へletterboxします。
4. 検出枠・信頼度と、取得・変換・推論・合計処理時間・撮影時点からの経過時間を出力します。

```text
HAND_FRAME seq=... capture_ms=... convert_ms=... infer_ms=... total_ms=... age_ms=... hands=... failures=...
HAND_BOX seq=... score=... x1=... y1=... x2=... y2=...
```

枠は回転後の240×320画像の座標です。信頼度のしきい値0.25、NMS 0.5はモデルの既定値です。
`infer_ms` はモデル前処理・推論・後処理を含み、`total_ms` はカメラ取得から検出完了までです。
シリアル出力やループ末尾の待機時間は含めないため、その逆数を実効fpsとみなさないでください。
`age_ms` はカメラのフレーム時刻から検出完了まで。バッファ待機があれば合計処理時間より長くなります。
初期評価ではフレームバッファ1個、推論と取得を直列に実行します。
30フレームごとに内部RAMとPSRAMの空き容量を出します。

## 試行の記録

HID用の`tools/record.py`とはログ形式が異なるため、こちらを使います。
デバイスのリセットやコマンド送信は行いません。`--out`は新しいファイルを指定します。
ただし、このWSL環境ではUSB Serial/JTAGポートの開閉に伴うハードウェアリセットを
観測しています。記録冒頭に起動・約4秒の準備時間が含まれる場合があります。

```bash
# リポジトリ直下
uv run --with pyserial python tools/record_hands.py --port /dev/ttyACM0 \
  --seconds 15 --label background --out recordings/hand-background-01.jsonl
```

まず背景のみ、開いた手、スタイラスを持った手をそれぞれ記録します。
次に左右スワイプ、人や物の通過、飲み物を取る動作、照明変更を比較します。
`frames_with_hand` は手が検出されたフレーム数であり、操作の認識率ではありません。
モデルには操作の意図を判定する機能はなく、手以外の誤検出や手の取りこぼしも評価対象です。

## 推論画像を確認する

シリアルで `s` を送ると、次の推論画像をテキスト形式で転送します。
通常の記録ツールを閉じてから、以下で1枚だけPPM画像を保存できます。

```bash
uv run --with pyserial python tools/snapshot_hand.py --port /dev/ttyACM0 \
  --out recordings/hand-palm-01.ppm
```

デフォルトでは最初のフレーム受信から5秒後に撮影を要求します。転送中は推論を中断するため、
この操作は速度測定とは分けてください。保存画像はGit管理対象外です。

## 初期結果（2026-10-13）

ESP-IDF 5.5.1でビルド・書き込み成功。モデル込みのアプリは2,619,664バイトです
（最初のスナップショット機能追加前）。黒画像での推論完了、GC0308と8MB PSRAMの初期化を確認しました。
最初の無ラベル記録40フレームは撮影失敗0、推論中央値171.8ms、取得＋変換＋推論中央値190.8ms、
撮影時点から検出完了までの中央値336.4msでした。動作中のPSRAM空きは約7.23MBです。

開いた手を約15秒見せた最初の試行では、記録81フレーム中、検出枠が出たのは6フレームでした。
信頼度は0.269〜0.378。起動待ちと手を出し入れする時間を含むため、6/81を検出精度とは扱いませんが、
安定した検出には至っていません。画像の向き・色・写り方を確認してから次の評価へ進みます。

## 参照

- [公式の手検出モデル](https://components.espressif.com/components/espressif/hand_detect/versions/0.2.0/readme)
- [ESP32カメラドライバー](https://components.espressif.com/components/espressif/esp32-camera/versions/2.1.8/readme)
