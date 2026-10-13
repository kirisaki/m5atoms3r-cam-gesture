# 開発・検証

ファームウェアはPlatformIO、認識・通信形式・HID状態管理のホストテストはCMake/CTestでビルドします。

```bash
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_SANITIZERS=ON
cmake --build build/host --parallel 2
ctest --test-dir build/host --output-on-failure
pio run -e atoms3r-cam -e atoms3r-receiver
```

ホストテストにはC++11対応コンパイラとCMake 3.16以上が必要です。
ASan/UBSanはGCC/Clangで有効です。制限された実行環境でLeakSanitizerが動かない場合のみ
`ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/host --output-on-failure` を使用します。
GitHub Actionsでもホストテストと両ファームのビルドを実行します。

## ファイル構成

| ファイル | 役割 |
| --- | --- |
| `src/main.cpp` | Arduinoのsetup/loopからアプリへ接続 |
| `include/camera_app.h` / `src/camera_app.cpp` | カメラの状態・処理順序、認識と各出力の接続 |
| `src/camera_console.cpp` | シリアル操作、状態表示、調整用画像・診断出力 |
| `include/camera_capture.h` / `src/camera_capture.cpp` | カメラ初期化・フレーム取得・平均輝度画像への縮小 |
| `include/optical_flow.h` / `include/gesture_detector.h` | Arduinoに依存しない移動推定・認識 |
| `include/gesture_config.h` | 認識設定・処理間隔・設置方向 |
| `include/gesture_output.h` | 反転設定を含むシリアルイベント名への変換 |
| `include/hid_shortcut.h` / `include/gesture_hid.h` | HID押下・解放の状態管理とUSB接続 |
| `include/gesture_protocol.h` | I2Cパケット、CRC、受信イベントの重複除外 |
| `include/gesture_i2c.h` / `src/gesture_i2c.cpp` | I2Cマスター送信 |
| `src/receiver_main.cpp` / `include/receiver_display.h` | I2C受信と液晶表示 |
| `test/` | 合成画像、通信形式、HID状態管理のテスト |
| `tools/` | 画像記録とホストでの再生 |

カメラと受信機は別ビルドです。`platformio.ini` で各環境のソースを明示しています。
しきい値・軸・反転設定は現在の設置に合わせた初期値であり、別の配置では調整が必要です。

## シリアル操作

| キー | 操作 |
| --- | --- |
| `i` / `h` | 状態、メモリ、撮影失敗数、イベント数を表示 |
| `b` | 追跡履歴をリセットし、約3秒待機 |
| `a` | 画像の横 / 縦のどちらを左右移動として追うか切り替え |
| `m` | 左右の割り当てを反転 |
| `d` | 毎秒5回の診断ログを切り替える |
| `p` | 検出を停止・再開 |
| `v` | 調整用の40×30画像列をシリアルへ出力（通常はオフ） |
| `0` / `1` | USB HIDキー入力だけ停止 / 再開 |
| `[` / `]` | Alt+左 / Alt+右を1回送信（HID有効時のみ） |
| `u` | 書き込みモードへ移行（撮影・I2C・HID停止。再起動またはuploadで復帰） |

`a`・`m`・再開時は追跡履歴もリセットします。
`state` は `CALIBRATING`（起動待機）、`READY`（検出待ち）、
`TRACKING`（移動量を蓄積）、`WAIT_CLEAR`（連発防止）の4状態です。
`WAIT_CLEAR` は動きが落ち着くと解除されます。空の背景を覚える方式ではありません。

診断ログの `flow` は縦横比と判定軸の傾きを補正した後の、軸方向・直交方向の移動量です。
`travel` は累積移動量、`inliers` は方向が揃った小領域数、`moving` は移動候補数、
`global` は広範囲の移動を検出したか、`flow_ms` はフロー計算時間です。

## 調整用の画像記録と再生

`tools/record.py` は40×30の画像列とイベントをローカルに保存します。キー入力は行いません。
シリアルモニターを閉じ、画像列出力がオフの状態から実行します。Python の `pyserial` が必要です。
処理時間の診断ログも保存する場合は、診断ログがオフの状態から `--diagnostics` を指定します。
`recordings/` は Git の対象から除外しています。

```bash
uv run --with pyserial python tools/record.py --port /dev/ttyACM0 --seconds 30 --out recordings/swipe.jsonl
```

`tools/replay.cpp` は同じ検出コードで保存画像を再生するツールです。
標準入力の各行に時刻（ms）と40×30画素の16進文字列を渡します。
出力は時刻・状態番号・方向の揃った領域数・移動候補数・軸に沿う/直交するフロー・累積移動量・イベントのCSVです。
引数 `y` で縦軸、それ以外では横軸を追います。

```bash
g++ -std=c++11 -O2 -Wall -Wextra -Werror -Iinclude tools/replay.cpp -o /tmp/atoms3r-replay
python3 -c 'import json; rows=map(json.loads,open("recordings/swipe.jsonl")); [print(r["ms"],r["gray"]) for r in rows if "gray" in r]' | /tmp/atoms3r-replay y
```

`record.py` はHIDを停止して記録し、終了後も停止したままです。通常操作へ戻す際は `1` を送ってください。
カメラポートのDTRは有効にします。画像出力・診断はオフの状態から記録を始めてください。
`recordings/` はGit管理対象外です。
