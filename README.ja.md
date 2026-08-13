# nss_stns

[English](README.md)

**NetBSD**、**FreeBSD**、**DragonFly BSD** 向けの [STNS](https://stns.jp) ネームサービススイッチモジュールです。

STNS 公式のクライアント [STNS/libnss](https://github.com/STNS/libnss) は glibc の NSS 向けです。本実装は BSD の `nsswitch(5)` モジュールインターフェースに対する別実装で、`getpwnam(3)`、`getgrnam(3)`、`getgrouplist(3)` などが STNS API サーバー上のユーザーとグループを解決するようにします。

NetBSD が基準プラットフォームです。このインターフェースを設計したのが NetBSD であり、`_r` 版に加えて非リエントラント版もディスパッチするぶん対応範囲も広いためです。FreeBSD と DragonFly はそこで動くものを適応させたものです。

設定ファイルは Linux で使っているものと同じ `stns.conf` です。

## 対応状況

| | NetBSD | FreeBSD | DragonFly |
| --- | --- | --- | --- |
| passwd の解決 (名前 / uid / 列挙) | ○ | ○ | ○ |
| group の解決 (名前 / gid / 列挙) | ○ | ○ | ○ |
| 補助グループ (`getgroupmembership`) | ○ | ○ | ○ |
| 非リエントラント版 `getpwnam(3)` / `getgrnam(3)` | ○ | 該当なし | 該当なし |
| `AuthorizedKeysCommand` 用 `stns-key-wrapper` | ○ | ○ | ○ |
| `cache-stnsd` の unix ソケット | ○ | ○ | ○ |
| CI | ○ | ○ | ○ |

MidnightBSD、GhostBSD、HardenedBSD といった FreeBSD 派生は `__FreeBSD__` を定義するため、同じ分岐でビルドされます。DragonFly の DPorts は FreeBSD Ports Collection から生成されるもので投稿先ではないため、port のエントリは2つではなく1つです。[ports-zakinko](https://github.com/zakinko/ports-zakinko) を参照してください。

OpenBSD と macOS は対象外です。どちらも nsswitch のモジュールインターフェース自体を持ちません。OpenBSD で差し込めるディレクトリソースは YP だけで、base に `ypldap(8)` があるのはそのためです。macOS は Open Directory を使います。どちらかに対応するとなれば、このモジュールの移植ではなくデーモンを書く話になります。

## ビルドとインストール

libcurl と BSD make が必要です。

```sh
# NetBSD
pkgin install curl
make
make install          # PREFIX の既定値は /usr/pkg

# FreeBSD / DragonFly
pkg install curl
make
make install          # PREFIX の既定値は /usr/local
```

`make install` が置くもの:

| ファイル | NetBSD | FreeBSD / DragonFly |
| --- | --- | --- |
| モジュール | `/usr/pkg/lib/nss_stns.so.0` | `/usr/local/lib/nss_stns.so.1` |
| モジュールの symlink | `/usr/lib/nss_stns.so.0` | `/usr/lib/nss_stns.so.1` |
| key wrapper | `/usr/pkg/bin/stns-key-wrapper` | `/usr/local/bin/stns-key-wrapper` |
| 設定サンプル | `/usr/pkg/share/examples/nss_stns/stns.conf` | `/usr/local/share/examples/nss_stns/stns.conf` |

この配置について、知っておく価値のある点が2つあります。

**なぜ `/usr/lib` に symlink するのか。** libc はモジュールを `dlopen("nss_stns.so.<version>")` と、パスなしのベース名だけで読み込みます。set-user-ID されたプログラムの場合、ランタイムリンカは信頼されたディレクトリ `/lib` と `/usr/lib` しか探索しません。したがって `LOCALBASE` の下にしか存在しないモジュールは、`su(1)` や `login(1)` をはじめ特権で動くものからは黙って読み込みに失敗します。symlink はそれを動かすためのものです。

**なぜバージョン接尾辞が異なるのか。** ファイル名の末尾は `NSS_MODULE_INTERFACE_VERSION` で、NetBSD では `0`、FreeBSD と DragonFly では `1` です。`Makefile` が `uname -s` から適切なものを選びます。

パスは通常どおり上書きできます:

```sh
make PREFIX=/opt/stns SYSCONFDIR=/etc install
```

パッケージはこのリポジトリには置いていません。他の自作ソフトウェアの分と一緒に、pkgsrc パッケージは [pkgsrc-zakinko](https://github.com/zakinko/pkgsrc-zakinko)、ports と DPorts を兼ねる port は [ports-zakinko](https://github.com/zakinko/ports-zakinko) にあります。`/usr/pkgsrc`、`/usr/ports`、`/usr/dports` に配置してビルドするコマンドも、それぞれの README に書いてあります。

## 設定

### nsswitch.conf

これは我々が置く場所を決められるファイルではありません。ベースシステムの持ち物であり、パスは `<nsswitch.h>` の `_PATH_NS_CONF` で固定されています。そのため、パッケージ自体をどこにインストールしたかに関わらず、いずれのシステムでも `/etc` に置かれます。`files` の後ろに `stns` を足してください:

```text
passwd: files stns
group:  files stns
```

`files` を先に置いておけば `/etc/passwd` のローカルアカウントが常に優先され、API サーバーに到達できないときもマシンは使える状態を保ちます。

> NetBSD の既定値は `passwd: compat` です。上記のように `files stns` へ変更するか、`compat` のまま `passwd_compat` と `group_compat` に `stns` を足してください。

### stns.conf

設定ファイルは以下のうち最初に見つかったものが読まれます。

1. `${SYSCONFDIR}/stns/client/stns.conf` — NetBSD では `/usr/pkg/etc/...` (pkgsrc の `PKG_SYSCONFDIR`)、FreeBSD では `/usr/local/etc/...`
2. `/etc/stns/client/stns.conf`

どちらも upstream STNS の `stns/client/` という階層を維持しており、素の `stns.conf` には平坦化していません。`nss-pam-ldapd` のような単一ファイルのモジュールなら平坦な `nslcd.conf` で済みますが、STNS はスイートです。サーバー側の設定が `stns/server/stns.conf` である以上、ここで平坦な名前にすると、これらのシステム向けに STNS サーバーがパッケージされた日に曖昧になります。`sssd` が設定を分けているのも同じ理由です。

2つ目のパスは Linux ホストが置く場所で、意図的に対応しています。Linux マシンから `stns.conf` をそのままコピーして置けます。キー名、テーブル、既定値はすべて `STNS/libnss` と一致します。

最小の例:

```toml
api_endpoint = "https://stns.example.com/v1"
auth_token   = "xxxxxxxxxxxxxxxx"

uid_shift = 10000
gid_shift = 10000

[tls]
ca = "/usr/pkg/etc/stns/client/ca.pem"
```

対応しているキーの全ては [`stns.conf.example`](stns.conf.example) を参照してください。

### パスワード

BSD に `/etc/shadow` はありません。そこで本モジュールは `files` バックエンドと同じ振る舞いをします。API から返ってきた `password` のハッシュを `pw_passwd` に入れるのは呼び出し元の実効 uid が 0 のときだけで、それ以外には `*` を返します。これにより、shadow データベースを使わずに `pam_unix(8)` で STNS ユーザーを認証できます。

### SSH 鍵

```text
# /etc/ssh/sshd_config
AuthorizedKeysCommand /usr/pkg/bin/stns-key-wrapper
AuthorizedKeysCommandUser nobody
```

`stns.conf` の `chain_ssh_wrapper` には、出力を続けて出す2つ目のコマンドを指定できます。既存の `AuthorizedKeysCommand` から段階的に移行できるようにするためのものです。

### cache-stnsd

API を直接叩く代わりに [cache-stnsd](https://github.com/STNS/cache-stnsd) を経由する場合:

```toml
[cached]
enable      = true
unix_socket = "/var/run/cache-stnsd.sock"
```

## 異常時の振る舞い

ネームサービスモジュールはマシン上の全プロセスの中で動きます。したがって、スループットより異常系の扱いが重要です。

- **レスポンスはディスクにキャッシュされます。** 場所は `cache_dir/<euid>/` で、リクエストパスをキーにします。ユーザーごとに専用のディレクトリが割り当てられ、自分が所有していないファイルの読み書きは拒否します。404 も長さ 0 のファイルとして記憶され、こちらは `negative_cache_ttl` という別の短い TTL を持ちます。`cache_dir` の既定値は NetBSD が `/var/db/stns`、FreeBSD と DragonFly が `/var/cache/stns` です。`hier(7)` が `/var/cache` を定めているのは後者だけで、前者には相当するものがないためです。
- **接続失敗はサーキットブレーカーを落とします。** `request_locktime` の間リクエストを試みなくなるので、到達できないサーバーのコストは「ルックアップごとに1回のタイムアウト」ではなく「1回のタイムアウト」で済みます。ブレーカーのファイルは誰でも書ける場所ではなく呼び出し元自身のキャッシュディレクトリに置かれるため、非特権ユーザーが全体の名前解決を止めることはできません。
- **API が返す id レンジのヒント** (`User-Highest-Id` など) を使い、サーバーが持ち得ない uid に対するリクエストを省きます。
- **`stns.conf` の知らないキーは報告されます。** プロセスごとに1回、`LOG_NOTICE` で出します。一方、キーが無いことは報告しません。ほぼすべてが省略可能で既定値が文書化されており、それをルックアップごとに言えば、正常な設定についての報告で syslog が埋まってしまいます。「有るが知らないキー」は話が別です。`api_endpont` と書いてしまった場合、そうでなければモジュールは黙って `localhost` を見に行き、どこにも何も出ないまま全ルックアップが失敗します。`[http_headers]` は設計上キーが自由なので対象外です。
- **名前は使用前に検証されます。** `[A-Za-z_][A-Za-z0-9._-]{0,31}` から外れるものはリクエストを出さずに拒否します。細工されたアカウント名によるクエリパラメータの注入を防いでいるのはこれです。
- **libcurl は `CURLOPT_NOSIGNAL` で動作します。** またモジュールがエクスポートするのは `nss_module_register` だけです。ホストプロセスのシグナル処理やシンボルテーブルを乱すことはありません。

## テスト

```sh
make test          # 単体テストと、libc と同じ手順でのモジュール読み込み
make symbols       # nss_module_register だけをエクスポートしていることの確認
make plist         # パッケージリストと staged install の突き合わせ。
                   # オーバーレイの clone が要る (tests/check_plist.sh 参照)
make ident         # 配布 tarball で $SNOWRABBIT$ が展開されることの確認
make external      # 同梱サードパーティコードとマニフェストの突き合わせ
make asan          # AddressSanitizer と UBSan の下での単体テスト
make integration   # 結合テスト。root と python3 が必要
```

**`make test`** は設定のパース (Linux 用に書かれた `stns.conf` をそのまま読ませるものを含む)、名前の検証、キャッシュキーのエスケープ、id レンジのヒント、バッファへの詰め込みを検査します。最後のものはバッファサイズを 0 から1バイトずつ増やしながら、そのすべてで綺麗に `ERANGE` が返ることを要求します。続いてビルドされたモジュールを `RTLD_NOW` で `dlopen` し、`nss_module_register` を呼んでメソッドテーブルを1件ずつ検査します。この最後の部分が重要で、モジュールの登録に失敗した場合、すべてのルックアップは黙って次のソースに落ちるため、外からは「正しく設定されているがディレクトリが空のホスト」と見分けがつきません。

**`make integration`** は [`tests/mock_stns_server.py`](tests/mock_stns_server.py) を起動し、`/etc/nsswitch.conf` をモジュールへ向けて `getent(1)`、`id(1)`、`stns-key-wrapper` から駆動します。名前と id によるルックアップ、空フィールドの既定値、列挙、補助グループのマージ、300人のグループ (libc 側の「バッファを広げて再試行」経路を通します)、id シフト、`auth_token` / BASIC 認証 / `http_headers` が実際にサーバーへ届いていること、安全でない名前がリクエストを一切出さずに拒否されること、ディスクキャッシュとネガティブキャッシュ (サーバーを止めた状態で)、非特権の呼び出し元、`query_wrapper`、`cache-stnsd` の unix ソケット、そして API に到達できないとき・設定が壊れているとき・設定が無いときにローカルアカウントが解決され続けることを確認します。

TLS 経由でも一通り走らせます。生成した CA を使い、各ケースを「失敗すべき対」と組にしています。正しい CA なら解決し信頼アンカー無しでは解決しない、`ssl_verify = false` は検証が拒否するものを受け入れる、サーバーが要求すればクライアント証明書を提示し無ければ拒否される、という具合です。「https で通る」ことだけを見るテストは、検証を切っていても同じように通ってしまいます。

エンドポイントは IPv6 でも、平文・TLS・クライアント証明書のすべてで通します。アドレスは2通りに書き分けます。`[::1]` と `[0:0:0:0:0:0:0:1]` は同じアドレスの別表記で、モジュールは渡されたものをそのまま curl に投げるだけだからです。`::1` を bind できないシステムでは、黙って通過させずログにその旨を出します。

意図的に扱いにくいレコードも2つ置いています。1人は鍵を10本持ち、最後の1本は長いコメント付きの RSA 4096 鍵に相当する長さです。ラッパーはその全部を、それぞれ欠けることなく返す必要があります。もう1人は gecos とホームディレクトリがどの初期バッファよりも大きく、ERANGE と再試行の経路を単体テストだけでなく libc 自身に歩かせます。

`/etc/nsswitch.conf` を書き換える (終了時に戻します) ので、VM か CI で実行してください。

**`make external`** は `external/` を [`external/MANIFEST`](external/MANIFEST) と突き合わせます。マニフェストには各同梱物の出所と、正確にどの upstream リビジョンなのかが記録してあります。NetBSD なら CVS の import タグが担う役割ですが、git に相当物が無いので書き下しています。チェックサムが合わない場合、誰かが同梱物を直接書き換えたということであり、リビジョンを記録している意味がなくなります。

CI はこれらすべてを NetBSD、FreeBSD、DragonFly で、push のたびと週1回実行します。VM イメージとそのパッケージセットは我々の足元で変化するもので (実際 DragonFly のイメージが自身の libssh2 では満たせない libcurl を積んでいたことがあります)、それに気付けるのは定期実行だけです。加えて週1回の別ジョブが、マニフェストのリビジョンが今も upstream の最新かどうか、そしてそのリビジョンの upstream が同梱物とバイト単位で一致するかを GitHub に問い合わせます。これが無いと、同梱パーサが何年も古いまま誰にも気付かれません。

## ライセンス

NetBSD に合わせた BSD 2-Clause です。一部は [STNS/libnss](https://github.com/STNS/libnss) から派生しており、`external/mit/` には [parson](https://github.com/kgabis/parson) と [tomlc99](https://github.com/cktan/tomlc99) を同梱しています。NetBSD が自身の `external/` ツリーをそうしているのに倣い、ディレクトリ名はライセンス名です。いずれも MIT です。詳細は [LICENSE](LICENSE) を参照してください。
