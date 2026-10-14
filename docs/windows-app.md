# Windows アプリの開発

.NET 10 のWPFアプリとコマンドラインツールで、上下左右・最大3ステップの割り当て変更と前面アプリに応じた自動切り替えを行います。
カメラがUSBキーボードとしてキーを送り、Windows側は設定用USB HIDで割り当てを指定します。
Windows標準のHIDドライバーで接続し、Windows側のキー注入は使いません。
WPFアプリでは画面からアプリ別の割り当てを保存し、トレイで常駐できます。CLIではJSONを指定して使います。

## WPFの設定画面とトレイ

Windows PowerShellでリポジトリのルートから起動します。

```powershell
dotnet run --project windows/GestureControl.Desktop
```

WSLから起動する場合はWindowsの.NETを使います。

```bash
'/mnt/c/Program Files/dotnet/dotnet.exe' run --project '\\wsl.localhost\Ubuntu-20.04\home\kirisaki\devel\m5atoms3r-cam-gesture\windows\GestureControl.Desktop\GestureControl.Desktop.csproj'
```

1. USB接続したカメラを自動検出します。複数台なら対象を選び、「再検索」で一覧を更新できます。
2. 左側でアプリを選び、右側でジェスチャと `Ctrl+Z` などのキーを指定します。
   「ジェスチャを追加」で `→ ↓`、`→ ↓ ←` などを追加できます。チェックを外すとアプリ全体を無効化します。
3. 「アプリを追加」で行を増やし、`notepad.exe` などの実行ファイル名を入力します。
4. 「保存して適用」で全行を検証して保存します。動作中なら旧接続を停止して新設定へ切り替えます。
5. 「開始」でアプリ別切り替えを有効にします。開始時も現在の編集内容を検証して保存します。

最初の行は、個別指定のないアプリに使う既定設定です。この行は削除できません。
初期設定はメモ帳が←/→、ペイントが無効、その他がAlt+←/Alt+→です。
設定画面自身が手前にある間は、ジェスチャによる入力を停止します。
メモ帳などへ移るとそのアプリの設定に切り替わります。

- **一時停止／再開**: カメラへSTOPを送り、接続を閉じます。再開時に前面アプリを調べ直します。
- **×で閉じる**: 画面を隠し、トレイで動作を続けます。編集中の内容もメモリー内に残ります。
- **トレイのダブルクリック**: 設定画面を再表示します。アプリを再度起動しても既存の画面を表示します。
- **トレイの右クリック**: 設定表示、開始／一時停止／再開、通常モードへ復帰、終了を操作できます。
- **終了**: 動作中ならSTOPを送って接続を閉じます。未保存の編集があれば保存・破棄・キャンセルを選べます。
- **通常のAlt＋左右へ戻す**: アプリ別切り替えを終了してカメラ単体の通常動作へ戻します。
  この操作の後はアプリを終了してもAlt+左右が使えます。

アプリの起動だけではカメラへ接続しません。再起動後も「開始」を押して有効にします。
Windowsログオン時の自動起動や自動開始は登録しません。
起動前にカメラが通常モードだった場合は、その通常動作が継続します。
他の設定用CLIは終了してから使ってください。シリアルでのログ閲覧は併用できますが、
動作中に別ツールから設定コマンドを送らないでください。

設定保存先は `%LOCALAPPDATA%\AtomS3RGesture\settings.json` です。
選択したカメラの識別番号と全プロファイルを1つのJSONに保存し、前回分を `settings.json.bak` に残します。
全設定の検証と一時ファイルへの書き込みが成功してからファイルを置き換えます。
不正なキー名や書き込み失敗の場合は、既存の保存設定と動作中の設定を保持します。
保存済みファイルが壊れている場合はエラーと初期値を表示し、確認して保存するまで開始しません。

試験用に `--settings C:\Temp\gesture-settings.json` で保存先を変更できます。
WSLの `\\wsl.localhost\...` ではWindowsのファイル置換が使えないため、設定の保存先には
Windows側のローカルフォルダーを使ってください。GUIの設定は `profiles` オブジェクト内に
CLIと同じプロファイル形式を持ちます。CLIの `--config` に指定する場合はそのオブジェクトを取り出します。

ビルド済みの `windows/GestureControl.Desktop/bin/Debug/net10.0-windows/GestureControl.Desktop.exe`
からも起動できます（Windowsの.NET 10 Desktop Runtimeが必要です）。
トレイと画面は同じ監視処理を使い、HID通信はUIとは別のワーカーで順番に実行します。

## ビルドと実行

Windowsの.NET 10 SDKを使います。編集はWSL内で行えます。
リポジトリのルートからWindows PowerShellで実行する例です。

```powershell
dotnet build windows/GestureControl.Cli
dotnet run --project windows/GestureControl.Tests
dotnet run --project windows/GestureControl.Cli -- devices
dotnet run --project windows/GestureControl.Cli -- info HID
```

WSLからはWindowsの実行ファイルとUNC形式のプロジェクトパスを指定します。

```bash
'/mnt/c/Program Files/dotnet/dotnet.exe' run --project '\\wsl.localhost\Ubuntu-20.04\home\kirisaki\devel\m5atoms3r-cam-gesture\windows\GestureControl.Cli\GestureControl.Cli.csproj' -- info HID
```

普段はCOM番号を使わず、`HID` を指定します。カメラはWindows側へ接続してください。
[HID通信仕様と検証手順](hid-control.md)に自動検出・転送方式をまとめています。
`HID` の代わりに `COM10` などを指定すると、開発用のシリアル互換接続になります。
`ports` は一覧だけを表示し、全COMポートに設定コマンドを送ることはありません。
COM指定時の `info` などは最初に既存の `i` コマンドで対応ファームウェアを確認します。
古いファームウェアには `@G1` を送信しません（旧版では数字の `1` がHID有効化になります）。

## 割り当てを試す

```powershell
# キーを送らず、左右設定の適用・読み戻し・通信断時停止を検証。終了後もHID停止。
dotnet run --project windows/GestureControl.Cli -- verify HID

# 実行中だけ左右で Ctrl+PageUp / Ctrl+PageDown を送る。
dotnet run --project windows/GestureControl.Cli -- run HID --left Ctrl+PageUp --right Ctrl+PageDown

# 10秒間、入力停止の状態で接続維持を検証する。
dotnet run --project windows/GestureControl.Cli -- run HID --disabled --seconds 10

# アプリが不要な通常動作（Alt+Left / Alt+Right）へ戻す。
dotnet run --project windows/GestureControl.Cli -- standalone HID
```

`run` は500msごとに通信を維持します。Ctrl+C・時間指定での終了時にはSTOPします。
プロセス強制終了や接続切断では、最後の通信から2秒でカメラ側が停止します。
通信エラーは表示して終了し、勝手に同じ割り当てを再開しません。
通常動作へ戻すには `standalone` を実行してください。

キー名: A〜Z、0〜9、F1〜F24、Left/Right/Up/Down、PageUp/PageDown、
Home/End、Insert/Delete、Enter/Esc/Backspace/Tab/Space。
修飾キー: Ctrl/Shift/Alt/Win と RCtrl/RShift/RAlt/RWin。
`None` で片方向だけ無効化できます。大文字小文字は区別しません。
キー名はUSBキーボード上のキーを表し、文字入力結果はWindowsの配列・IME設定に依存します。

## 組み合わせジェスチャ

カメラには `atoms3r-cam-sequence-hid` を書き込みます。
起動時のHIDは停止状態で、アプリの「開始」または明示的な通常モードへの変更で有効になります。
既存の `atoms3r-cam-sequence` は計測専用で、引き続きHIDを有効化できません。
受信ディスプレイは既存のI2C v2版をそのまま使えます。

- 単独4方向、2ステップ12通り、3ステップ36通りの計52通りです。
- 同じ方向が続く動きは1ステップです。`→ →` は別ジェスチャにしません。
- 約400ms動きが止まった時点で経路を確定し、その経路のキーだけを1回送ります。
  `→ ↓ ←` の途中で `→` や `→ ↓` のキーは送りません。
- 未割り当て・`None`・中止された経路では入力しません。短い経路の割り当てへ代替しません。
- 確定前にアプリの切り替え・停止・設定変更・通信期限切れがあると、その動作の入力を破棄します。
- 検出自体は光学フローです。動作の途中で止めたり範囲外へ出たりすると、短い経路として確定することがあります。

旧バージョン1の左右設定は読み込み時に移行し、次の保存からプロファイル形式をバージョン2にします。
既存の左右キーとアプリ全体の有効/無効は保ち、上下・組み合わせは未割り当てです。
GUI設定ファイル外側はバージョン2で、カメラ識別番号を保存します。
バージョン1も読み込め、旧COM番号は互換データとして保持します。

CLIの `watch --config` でも同じ形式が使えます。例:

```json
{
  "version": 2,
  "default": {"enabled": true, "bindings": {"L": "Alt+Left", "R": "Alt+Right"}},
  "applications": [
    {"executable": "notepad.exe", "enabled": true,
     "bindings": {"L": "Left", "R": "Right", "U": "Up", "D": "Down", "RD": "Ctrl+Z", "RDL": "Home"}}
  ]
}
```

アプリごとの設定は経路全体の置き換えです。指定しなかった経路は無効になります。
設定用HIDがない旧ファームウェアにはCLIのCOM指定で接続できます。左右専用版では、
上下・組み合わせの割り当てがある場合は更新が必要と表示します。WPFアプリには設定用HID対応版が必要です。
`verify HID` は全52経路の転送と読み戻しも確認し、キーを送らず終了時に停止します。

## 開発用シリアル通信仕様 G2

接続確認の `i` に対する `# Control: @G2 ...` を確認するとG2を使用します。
この行は互換用G1行より先に出力されます。GET/KEEP/STOP/STANDALONEの意味と2秒の通信期限はG1と同じです。

```text
@G2 1 SET 1 2 RD 1 29 RDL 0 74
@G2 1 OK ATOMS3R_CAM MANAGED 1 1 3 2 RD 1 29 RDL 0 74
```

SETの引数は「有効、件数、経路・修飾・usageの組」です。上の例は `→ ↓ = Ctrl+Z`、`→ ↓ ← = Home`。
経路は大文字のL/R/U/Dで1〜3文字、隣接重複なし、件数は0〜52です。
重複経路や不正な値は全体を拒否し、設定を部分適用しません。件数0で全経路を未割り当てにできます。

OK応答は「機種、モード、有効、期限内、最大ステップ数、件数、経路・修飾・usageの組」です。
最大ステップ数3は4方向と組み合わせ、1は従来の左右のみ対応を示します。
無効な割り当ては応答から省略します。アプリは全経路を読み戻して照合します。
G2はG1のエラーに加えPATH（不正・重複経路）、UNSUPPORTED_PATH（そのビルドでは認識できない経路）を返します。
G1 SETとSTANDALONEでは上下・組み合わせを消し、左右だけに置き換えます。

## 通信仕様 G1

115200 bps、DTR有効、RTS無効、ASCII、LF終端（CRLFも可）。
接続時はLFを1つ送り、前接続で途切れたフレームを終端します。
`@` からLFまでは1フレームとして消費し、不正・長すぎるフレームの内容も
既存の1文字コマンドとしては解釈しません。最大1023バイト（LFを除く）です。
未完フレームはLFまで保持し、時間経過で内容を既存コマンドへ戻すことはありません。

要求IDは1〜4294967295、数値はすべて10進です。

```text
@G1 1 GET
@G1 2 SET 4 80 4 79 1
@G1 3 KEEP
@G1 4 STOP
@G1 5 STANDALONE
```

- GET: 状態の読み出し。通信監視の期限は延長しません。
- SET: 左修飾ビット、左HID usage、右修飾ビット、右HID usage、有効フラグ。
  両方向を一括適用し、管理モードに入って2秒の期限を開始します。
- KEEP: 有効な管理セッションの期限を延長します。期限切れ・STOP後にはEXPIREDを返し、再開しません。
- STOP: 管理モードのまま入力を停止します。
- STANDALONE: 通常モードに戻し、Alt+Left/Alt+Rightで入力を再開します。

SET・モード変更・停止では押下中のキーを解放します。送信が失敗した場合は
解放を再試行し、解放できるまで次の押下は送りません。イベントの後送りはしません。
修飾ビットはUSB標準の8ビット、usageは4〜115。無効方向は修飾0・usage0です。
有効フラグは0/1。不正なSETは片方向だけも適用せず、既存設定を保ちます。

応答例（ID、OK、機種、モード、有効、期限内、左修飾、左usage、右修飾、右usage）:

```text
@G1 2 OK ATOMS3R_CAM MANAGED 1 1 4 80 4 79
@G1 4 OK ATOMS3R_CAM MANAGED 0 0 4 80 4 79
@G1 7 ERR RANGE
```

エラーは FRAME / FORMAT / RANGE / COMMAND / EXPIRED / MEASUREMENT。
IDを解釈できないフレームはID=0です。診断ログやジェスチャイベントは応答と混在します。
クライアントはIDが一致したACKを待ち、SETの全値を読み戻して照合します。
通信ライブラリは単一の所有スレッドから順番に呼び出します。

管理モード中の既存 `1` コマンドは再開させません。`0` は管理セッションも停止します。
`r` で計測中、または `GESTURE_MEASUREMENT_ONLY` ビルドではSET/STANDALONEを拒否します。
`[` / `]` は現在の左/右割り当ての手動テストです。
既存の `ALT_LEFT` / `ALT_RIGHT` ログは互換性のため方向名として維持します。
実際のキーは `# HID LEFT/RIGHT ... modifiers=... usage=...` で確認します。
I2Cの方向通知・ディスプレイ表示は割り当てによらず継続します。

設定はRAMのみです。カメラのリセット後は通常モード・既定割り当てに戻ります
（計測用ビルドでは引き続きHID停止）。アプリ管理モードを起動後も固定する設定は未実装です。

前面アプリ別の自動切り替えには下記の `watch` を使います。
WPFアプリからも同じ監視処理を開始・一時停止できます。

シリアル通信には公式の [System.IO.Ports](https://www.nuget.org/packages/System.IO.Ports/10.0.0) を使用しています。

## 実機確認（2026-10-14）

`atoms3r-cam-fast-crop-hid` をカメラへ書き込み、Windows COM10から確認しました。
下側25%除外・最低速度40・I2C表示は引き続き有効です。

- CLIのverify: 左右設定の読み戻し、KEEPでの継続、2秒の期限切れ、
  KEEPだけでは再開しないこと、再設定、STOPを確認。キー出力は行いません。
- CLIのrun: 左=Left、右=Right、修飾なしを45秒間適用。
  実機ログで左→右→左の送信（usage80/79/80、modifiers0）を確認し、
  メモ帳のカーソルが左右とも動いたことを利用者が確認しました。
- 通常ビルドのホストテスト6件とASan/UBSan付きの同6件が成功。
  Windows .NET 10で通信処理のテスト成功、CLIビルド警告・エラーなし。
  HID版・計測版の両ファームウェアをビルドしました。

上記は固定割り当ての初期確認です。アプリ別切り替えの仕様と検証は以下を参照してください。


## 前面アプリに応じて自動切り替え

```powershell
# USBに触れず、手前に出したアプリの実行ファイル名を表示する。
dotnet run --project windows/GestureControl.Cli -- foreground --seconds 30

# サンプル設定で自動切り替え。Ctrl+Cで停止する。
dotnet run --project windows/GestureControl.Cli -- watch HID --config windows/profiles.example.json

# 60秒だけ動作を試す。
dotnet run --project windows/GestureControl.Cli -- watch HID --config windows/profiles.example.json --seconds 60
```

WSLからの実行では、`--config` のファイルにもWindowsから読めるUNCパスを指定してください。
設定例は [profiles.example.json](../windows/profiles.example.json) です。
メモ帳 (`notepad.exe`) では通常の←/→、ペイント (`mspaint.exe`) では無効、
それ以外のアプリでは既定のAlt+←/Alt+→になります。

各アプリの `executable` は拡張子 `.exe` を含む実行ファイル名です。大文字小文字は区別しません。
フルパス・ワイルドカードには対応していません。同名の実行ファイルは同じ設定になります。
ブラウザー内のサイトやタブごとの判定は行いません。
同じ実行ファイル名の重複、未知の設定項目、欠けた割り当て、不正なキー名は起動前に拒否します。
`enabled: false` でアプリ単位の無効化、`left` または `right` の `None` で片方向を無効化できます。
設定ファイルは起動時に読み込むので、変更後はwatchを終了して起動し直してください。

前面ウィンドウを100ms間隔で調べ、変化を検知したら旧設定の入力を停止します。
同じウィンドウを100ms以上続けて確認した後、新しい設定を適用し、ACKの値を照合します。
設定の応答待ち中にウィンドウが変わった場合も再停止して調べ直します。
取得不能・権限不足・ロック画面など、前面アプリを判定できない状態では入力停止を維持します。
取得できなかったときに既定の有効設定へ切り替えることはありません。

**フォーカス変更とキー入力の切り替えは完全に同時ではありません。**
監視間隔とUSB通信の遅延があり、通常は切り替え後しばらく入力を停止してから再開します。
アプリを切り替えた直後のスワイプは避け、約0.5秒待って操作してください。
負荷や通信状況によって遅延が伸びる場合もあります。

通信エラーや機器側の設定不一致では入力停止を試み、接続を閉じて2秒後に再接続を試みます。
HIDではカメラを再検出し、個体を選択している場合はそのUSBシリアル番号を照合します。
再接続後は対応ファームウェアを確認してからSTOPし、その時点の前面アプリを調べ直します。
直前のアプリ設定を無条件で再送しません。
CLIのCOM指定では同じポート番号だけを再試行するため、番号が変わった場合は指定し直してください。
未対応ファームウェアには識別の読み取りだけを行い、設定コマンドを送りません。

Ctrl+C・`--seconds` による通常終了ではSTOPします。
強制終了やUSB切断ではカメラの2秒タイムアウトが入力を停止します。
`atoms3r-cam-sequence-hid` はリセット後も入力停止で起動します。
旧来の単方向HID版は起動時に通常モードへ戻るため、watchが再接続するまで既定のキーが有効になります。
終了後に通常のAlt+矢印を使うには `standalone HID` を実行してください。

前面アプリの取得にはWindowsの
[GetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow) と
[QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew) を使います。
ウィンドウのタイトルや入力内容は取得しません。ログには実行ファイル名と適用した設定を出します。

### 自動切り替えの検証（2026-10-14）

- Windowsのビルドは警告・エラーなし。設定ファイルの検証、既定設定、
  アプリ別無効化、前面取得不能、通信断、再接続中のフォーカス変化、
  SET応答待ち中のフォーカス変化、応答不一致、終了時STOPを自動テストしました。
- COM10でwatchを実行し、メモ帳→Chrome→メモ帳に応じて
  modifiers0→4→0、usage80/79の切り替えとキー送信を確認しました。
  メモ帳のカーソル移動、Chromeの戻る／進むが両方動くことを利用者が確認しました。
- キー無効の設定で実際にカメラUSBを抜き差しし、ポート切断→COM10不在→
  自動接続→現在の前面アプリ取得→設定再適用を確認しました。
  usbipdによる使用中USBの移動はWindowsに拒否されたため、物理的な抜き差しで確認しています。


### WPFアプリの検証（2026-10-14）

- .NET 10でビルド成功、警告・エラーなし。設定の往復変換、保存・バックアップ、
  不正な保存による既存ファイル保護、ワーカーの交換・一時停止・終了を自動テストしました。
- WindowsのUI Automationで、行の編集と保存、不正キーのエラー表示とファイル保護、
  COM10への接続・一時停止・再開、×でトレイ収納、二重起動による既存画面の復帰、
  未保存時の保存・キャンセル・不正な設定による終了中止、動作中の終了、再起動後の設定読み込みを
  確認しました。全アプリを無効にした試験用設定を使っています。
- 起動・画面操作の確認にはWindows側の一時保存先を使い、通常のユーザー設定とは分けています。
