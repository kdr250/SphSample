# SPH Sample
『Implementing SPH in 2D』学習用レポジトリ

## ビルド方法
### ネイティブの場合
[Build a project](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/how-to.md#build-a-project) を参考にして、ビルドして実行。

### Webの場合
1. `emcmake cmake -B build-web -G Ninja` を実行。
2. `cmake --build build-web` を実行。
3. `python server.py` を実行してWebサーバー起動。
4. ブラウザで `http://localhost:8000/main.html` にアクセスして確認。

## 参考にしたURL
- Schuermann, Lucas V. (Jul 2017). Implementing SPH in 2D. Writing.<br>
https://lucasschuermann.com/writing/implementing-sph-in-2d

- GitHub - mueller-sph<br>
https://github.com/lucas-schuermann/mueller-sph
