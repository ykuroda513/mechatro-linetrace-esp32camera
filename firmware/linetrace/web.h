// =============================================================
//  web.h  Wi-Fi アクセスポイント + 調整用HTTPサーバ
//
//  エンドポイント
//   GET /            調整ページ
//   GET /frame       最新画像 [w:u16][h:u16][画素 w*h byte]
//   GET /status      検出結果・制御出力 (JSON)
//   GET /params      パラメータ一覧と現在値 (JSON)
//   GET /set?kp=1.0  パラメータ変更 (複数可)
//   GET /run?on=1    走行開始 / on=0 停止
//   GET /save        NVSに保存
//   GET /reset       初期値に戻す
// =============================================================
#pragma once

void webBegin();
