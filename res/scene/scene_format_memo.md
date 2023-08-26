# 前提.
//　※ 「-Path:」までを一つの文字列として解釈するためコロンの前にスペースは不可.  
// exportが2つ以上ある場合は後勝ち.  
// iblLは2つ以上ある場合は後勝ち.  
// nameやpathには""を含んではいけない。また途中の空白も許さない.

# エクスポート設定.
export {  
   -Path: path  
};  

# IBL設定.
ibl {  
   -Path: path  
};  

# マテリアル設定.
material {  
   -Tag: name  
   -BaseColor: r g b a  
   -Occlusion: value  
   -Roughness: value  
   -Metalness: value  
   -Ior: value  
   -Emissive: r g b s  // 4要素目はRGBにかかるスケール値.  
   -BaseColorMap: path  
   -NormalMap: path  
   -OrmMap: path  
   -EmissiveMap: path  
};  

# モデル設定.
model {
   -Tag: name  // 省略不可.  
   -Path: path // 省略不可.  
};  

# インスタンス設定.
instance {  
   -Tag: name  
   -Mesh: name      // 省略不可. OBJファイル名の中に含まれるメッシュ名なので注意!  
   -Material: name  // 省略不可.  
   -Scale: x y z  
   -Rotation: x y z  
   -Translation: x y z  
};  

# ディレクショナルライト設定.
directional_light {  
   -Tag: name
   -Direction: x y z  
   -Intensity: r g b
};  

# ポイントライト設定.
point_light {  
   -Tag: name
   -Position: x y z  
   -Radius: value  
   -Intensity: r g b  
};  