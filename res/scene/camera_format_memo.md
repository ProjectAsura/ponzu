# カメラシーケンスデータフォーマット.
// exportが2つ以上ある場合は後勝ち.  
// nameやpathには""を含んではいけない。また途中の空白も許さない.

# エクスポート設定.
export {
    -Path: path
};

# カメラデータ.
camera {
    -FrameIndex: number
    -Position: x y z
    -Target: x y z
    -Upward: x y z
    -FieldOfView: fovY
    -NearClip: distance
    -FarClip: distance
};

