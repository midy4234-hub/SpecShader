#pragma once

// 画面に出さない OpenGL 3.3 Core のコンテキスト。エディタが閉じていても GLSL を走らせるために、
// プラグイン本体のスレッドが持つ。Mac は CGL、Windows は隠しウィンドウ + WGL。
// create() / makeCurrent() は同じスレッドから呼ぶこと。

#include <juce_core/juce_core.h>
#include <memory>

class OffscreenGL
{
public:
    OffscreenGL();
    ~OffscreenGL();

    // 作って current にし、juce::gl の関数を読み込む。失敗したら理由を返す
    bool create (juce::String& error);
    void destroy();
    bool isValid() const;
    // Windows: 隠しウィンドウ宛てのメッセージを捌く (放置するとシステムの一斉通知を待たせる)。スレッドのループから呼ぶ
    void pump();

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};
