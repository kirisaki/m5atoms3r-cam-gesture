# USB HIDによる設定通信

カメラはキーボードと設定用HIDコレクションを同じUSB接続で公開します。
キー入力は従来どおりカメラから送信します。Windowsアプリは設定用HIDを自動検出し、
割り当て・有効状態・通信期限を管理します。通常の操作にCOMポートは使いません。
USB CDCはログ、計測、書き込みモードへの切り替えと既存CLIの互換用に残しています。

WindowsアプリはSetupAPIでHIDを列挙し、VID/PID `303A:1001`、Usage Page `FF00`、Usage `0053`、
Feature Report長64バイト、`AGH1`シグネチャ、USBシリアル番号を確認します。
候補が1台なら自動接続し、複数なら選択を要求します。個別選択時はシリアル番号を保存し、
再接続時も同じ個体を検索します。別プロセスからの同時操作はデバイスごとの名前付きMutexで防ぎます。
列挙・識別では設定を変更しません。

追加の専用ドライバーやNuGetライブラリは不要で、Windowsの
[HidD_SetFeature](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_setfeature) /
[HidD_GetFeature](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/nf-hidsdi-hidd_getfeature)
をC#から呼び出します。Linux用のホスト実装はまだ追加していません。

## 実機確認

```powershell
dotnet run --project windows/GestureControl.Cli -- devices
dotnet run --project windows/GestureControl.Cli -- info HID
# 全52件の設定・通信期限・停止を確認。キーは送らず、終了時にも停止。
dotnet run --project windows/GestureControl.Cli -- verify HID
# アプリ別切り替え
dotnet run --project windows/GestureControl.Cli -- watch HID --config windows/profiles.example.json
```

従来の `info COM10` / `verify COM10` などは開発用として引き続き使えます。
HID管理中に別ツールからシリアルで設定を変更すると、世代番号・読み戻しの不一致で管理側が停止します。

## Feature Report AGH1

Report IDは6、IDを含めて64バイトです。整数はリトルエンディアン。
以下のオフセットはReport IDを除いた63バイトのペイロードに対するものです。

| オフセット | 内容 |
|---|---|
| 0–3 | ASCII `AGH1`（プロトコルの識別子と版） |
| 4 | 操作 |
| 5 | 応答コード。要求では0 |
| 6–9 | 要求ID、0は予約 |
| 10 | ページ番号、0開始 |
| 11 | 割り当て総数、0〜52 |
| 12 | フラグ |
| 13 | 応答: 最大ステップ数1または3。要求: 0 |
| 14–17 | 要求: 設定トランザクションID。応答: 設定世代番号 |
| 18–62 | 15件までの「経路、修飾、HID usage」の3バイト組 |

経路はL=1/R=2/U=3/D=4を桁にした5進数です。例: RD=14、RDL=71。
1〜3桁で、0の桁や連続する同一方向を含む経路は不正です。無効な割り当ては省略します。
使用しないフィールド・末尾は0。重複経路、不正なキー、ページ順序違いは拒否します。

操作はINFO=1、BEGIN=2、PAGE=3、COMMIT=4、KEEP=5、STOP=6、STANDALONE=7。
応答フラグは管理中=1、有効=2、期限内=4、計測ロック=8。
BEGIN要求だけは有効フラグ2を使えます。他の要求のフラグは0です。

- **INFO**: 指定ページの現在の設定を読みます。通信期限を延長しません。
- **BEGIN**: 新しいトランザクションID、総数、有効状態を指定します。現在の設定はまだ変更しません。
- **PAGE**: 同じID・総数でページを順番に送ります。最後のページ以外は15件です。
- **COMMIT**: 同じIDですべてのページの受信を確認してから一括適用します。
  0件ならBEGIN直後にCOMMITし、全経路を未割り当てにできます。
- **KEEP / STOP / STANDALONE**: シリアル版と同じ意味です。STOPとSTANDALONEは転送中の設定も破棄します。

BEGINから1500ms以内に完了しない転送は適用できません。転送中にSTOP、設定変更、通信期限切れがあって
世代が変わった場合もCOMMITを拒否します。COMMIT後の再送は再適用しません。
要求IDを照合してACKを待ち、設定後は全ページの世代・フラグ・内容を読み戻して一致を確認します。
USB受信コールバックは報告のコピーだけを行い、カメラのループがコマンドを実行します。
未処理要求は1つまでで、受信後1秒を超えた要求は実行しません。

応答コード: OK=0、FORMAT=1、RANGE=2、PATH=3、EXPIRED=4、MEASUREMENT=5、UNSUPPORTED_PATH=6、CHANGED=7。
COMMITの応答が失われた場合もホストは終了時にSTOPを試みます。切断・プロセス終了時は従来どおり
最後のSET/KEEPから2秒でカメラが入力を停止します。
