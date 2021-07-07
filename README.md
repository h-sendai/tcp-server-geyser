# server-geyser

定期的にデータを送らなくなるサーバー。

```
% ./server-geyser -h
Usage: server [-b bufsize (1460)] [-r rate]
-b bufsize:    one send size (may add k for kilo, m for mega)
-p port:       port number (1234)
-r rate:       data send rate (bytes/sec).  k for kilo, m for mega
-D data_send_sec: data send seconds.  default 10 seconds
-R data_rest_sec: data rest seconds.  default 10 seconds
```

デフォルトは10秒間データ送信し、その後10秒間データを送らなくなる。

時間を変更するのは``-D data_send_sec``と``-R data_rest_sec``を使う。

```
 <-- data_send_sec --> <------- data_rest_sec ------->
 +--------------------+                              +------
 |                    |                              |
_|                    |__ ___________________________|
------------------------------------------------------------> t
```

-r rateが指定されたらデータ送信再開後のレートも指定されたレートになるように
してある。

クライアントが接続を切ったらその旨ログを出力する。
データ送信の休止はたんに``sleep(data_rest_sec)``しているだけなので
データを送っていない間に切られるとログを出力するのが遅くなる
（select()なりepoll()なりを使ってTCP FINを読めるようにすればよいのだが
まだ実装していない）。
