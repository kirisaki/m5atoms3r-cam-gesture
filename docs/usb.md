# USBキーボードと再書き込み

PCにつなぐのはカメラ側です。USB OTG（TinyUSB）でキーボードHIDと
診断用シリアル（CDC）を同時に公開します。受信機のI2C表示も継続します。
起動時はHID有効です。認識1回につき左Altと矢印を同時に押し、約40ms後に両方離します。
USB未接続・送信中のイベントは蓄積せず、再接続時にも再送しません。
解放の送信に失敗した場合は再試行し、キーを押している間は撮影を待たせます。

シリアルの `0` でHIDだけ停止、`1` で再開できます。認識ログとI2Cは継続します。
`[` はAlt+左、`]` はAlt+右を各1回送る手動テストです（HID有効時のみ）。
`i` の `# HID` 行で有効状態、送信可能状態、送信数・スキップ数を確認できます。
`SENT` はUSB転送完了を表し、アプリ側でショートカットが実行された保証ではありません。

Windowsへ入力する場合、カメラをWSLから切り離してください。
WSLへ接続中のUSBデバイスはWindowsから使えません。
Windowsの `usbipd list` でカメラのBUSIDを確認し、
`usbipd detach --busid <BUSID>` でWindowsへ戻します。
再びWSLで書き込む際は `usbipd attach --wsl --busid <BUSID>` を使います。
USB構成変更でVID:PIDやポート番号が変わるため、必要に応じてbind・attachし直します。
手順は[MicrosoftのUSB接続ガイド](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)を参照してください。

### USBからの再書き込み

WindowsでCDCポートを閉じた際、自動リセットが誤って作動し、カメラが
書き込み待ちへ移る現象を実機で再現しました。この間はI2Cも止まり、受信画面はWAITになります。
これを防ぐため、CDC制御線・1200bps設定による自動リセットは無効です。
再書き込み時は通常のシリアル接続（115200bps、DTR有効）で `u` を送ります。
カメラが書き込み用USBに切り替わったら、必要に応じてWSLへattachし直し、
ポートを確認してREADMEのuploadを実行します。物理ボタンでの書き込みモード移行も可能です。
`u` を送ると撮影・I2C・HIDが止まるため、通常の使用中には送らないでください。
書き込みをせず通常起動へ戻す場合は、WSLに接続した書き込み用ポートに対して
`uv run --with esptool==4.11.0 esptool.py --chip esp32s3 --port /dev/ttyACM0 --before default_reset --after hard_reset run`
を実行します。`--before no_reset` では書き込み待ちに残る場合があります。

`tools/record.py` は記録開始時にHIDを停止し、終了後も停止したままにします。
記録後にキー入力を使う場合は `1` を送るか再起動してください。

押下・解放、無効化、切断・再接続、送信失敗時の解放、時刻の折り返しをホストで検証できます。

```bash
g++ -std=c++11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude test/hid_shortcut_test.cpp -o /tmp/atoms3r-hid-test
ASAN_OPTIONS=detect_leaks=0 /tmp/atoms3r-hid-test
```
