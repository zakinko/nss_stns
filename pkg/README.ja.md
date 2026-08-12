# パッケージング

[English](README.md)

上流の `Makefile` は BSD make で、`PREFIX`、`SYSCONFDIR`、`LIBDIR`、`BINDIR`、`EXAMPLESDIR`、`NSSLIBDIR`、`DESTDIR` を受け取ります。そのため各コレクション向けのパッケージは薄いものになっています。

`pkg/` 以下では、各パッケージがそれぞれのコレクションでの位置と同じパスに置いてあります。配置は素の `cp -R` で済みます。

| コレクション | ディレクトリ | カテゴリ |
| --- | --- | --- |
| pkgsrc (NetBSD) | [`pkgsrc/security/nss_stns`](pkgsrc/security/nss_stns) | `security`。`nss-pam-ldapd` の隣 |
| ports (FreeBSD) | [`ports/net/nss_stns`](ports/net/nss_stns) | `net`。`nss-pam-ldapd` や `nss_ldap` の隣 |

カテゴリが違うのはコレクション自体が違うからです。pkgsrc はディレクトリクライアントを `security` に、ports ツリーは `net` に置いています。

## pkgsrc

pkgsrc は NetBSD 専用ではありません。FreeBSD でも bootstrap でき、そこでのモジュールは `nss_stns.so.1` になります。パッケージは `OPSYS` からそれを判断するので、1つのパッケージディレクトリで両方を賄えます。nsswitch のモジュール機構が無いプラットフォームで試されないよう `ONLY_FOR_PLATFORM` を宣言してあります。

```sh
cp -R pkg/pkgsrc/security/nss_stns /usr/pkgsrc/security/
cd /usr/pkgsrc/security/nss_stns

make makesum          # リリース tarball を取得して distinfo を生成
make install
```

依存をすべてソースからビルドしてもこのパッケージについて何かが分かるわけではなく、時間だけが何時間もかかります。バイナリパッケージで引き込むには:

```sh
make DEPENDS_TARGET=bin-install install
```

どこかへ送る前に、pkgsrc 自身のレビューアを通してください:

```sh
pkg_add pkglint
pkglint .
```

元に戻すには:

```sh
make deinstall
```

## ports

```sh
cp -R pkg/ports/net/nss_stns /usr/ports/net/
cd /usr/ports/net/nss_stns

make makesum          # リリース tarball を取得して distinfo を生成
make install clean
```

どこかへ送る前に stage して、パッケージリストと実際にインストールされたものをフレームワークに突き合わせさせてください:

```sh
make stage
make check-plist
make package
```

元に戻すには:

```sh
make deinstall
```

## リリースではなく作業ツリーからビルドする

いずれのパッケージも GitHub からリリース tarball を取得します。作業ツリーの方をビルドしたい場合は、フレームワークが期待する名前で tarball を作って渡します:

```sh
V=0.1.0
mkdir -p /tmp/dist/nss_stns-$V
tar --exclude .git -cf - . | (cd /tmp/dist/nss_stns-$V && tar -xf -)

# pkgsrc
tar -C /tmp/dist -czf /usr/pkgsrc/distfiles/nss_stns-$V.tar.gz nss_stns-$V

# ports
tar -C /tmp/dist -czf \
    /usr/ports/distfiles/zakinko-nss_stns-v${V}_GH0.tar.gz nss_stns-$V
```

こうしておくとパッケージディレクトリでの `make makesum` は取得しに行かず既にある tarball を拾い、あとは通常どおりビルドが進みます。Packaging ワークフローがやっているのもこれです。

なお、この方法で作った tarball の `stns.conf.example` は `$SNOWRABBIT$` の ident 行が未展開のままです。`tar` は `git archive` ではないためです。そこが問題になるなら `git archive --prefix=nss_stns-$V/ -o <file> HEAD` を使ってください。

## どのパッケージにも手当てが要る唯一の点

`make install` は通常、モジュールを `/usr/lib` へ symlink します。libc が `dlopen("nss_stns.so.<version>")` とベース名だけで呼び、ランタイムリンカは set-user-ID されたプログラムを `/lib` と `/usr/lib` に限定するためです。symlink が無いと `su(1)` や `login(1)` は STNS アカウントの解決に黙って失敗します。

このパスは `PREFIX` の外であり、どのパッケージマネージャも stage しません。そこでどのパッケージも `NSSLIBDIR=${PREFIX}/lib` でビルドして symlink を抑止し、代わりに自前のフックで作成します:

- pkgsrc: `INSTALL` と `DEINSTALL` スクリプト
- ports: `pkg-plist` の `@postexec` と `@postunexec`
