# Vulkan 化作業メモ

このメモは、CPU 版レイトレーサを Vulkan Compute Shader へ段階的に移していくために、ここまで行った準備と実装を振り返るためのものです。

CUDA 版と同じく、CPU 版の完成形を一気に移植せず、小さい処理から順に GPU 側へ移していく方針です。

## マイルストーン

- [x] CPU 版レイトレーサの基準実装を取り込む。
- [x] CPU 版を低解像度でレンダリングして、取り込みが正しいことを確認する。
- [x] Vulkan SDK と shader compiler を確認する。
- [x] `techmadot/vulkan-practical-guide-samples` の CMake / shader compile 構成を確認する。
- [x] `CompileShaders.cmake` 相当の小さい CMake 関数を追加する。
- [x] CMake で Vulkan を optional にし、`Vulkan::Vulkan` へリンクする。
- [x] GLSL compute shader を `glslangValidator` で SPIR-V に変換する。
- [x] `src/vulkan_renderer.cpp` / `src/vulkan_renderer.h` を追加する。
- [x] `main.cpp` から `--renderer vulkan` で Vulkan 側を呼べるようにする。
- [x] Vulkan Compute Shader で背景グラデーションを生成し、PPM として保存する。
- [x] 球 1 個との交差判定を Compute Shader 側で行う。
- [x] 複数の球を GPU buffer で扱う。
- [x] Lambertian 風の diffuse bounce を追加する。
- [x] ランダムサンプリングと複数 bounce を Compute Shader 側に移す。
- [x] `metal` / `dielectric` を追加する。
- [x] カメラ設定、defocus blur、ランダムシーン生成を移植する。
- [ ] CPU 版 / CUDA 版 / Vulkan 版のレンダリング時間を比較する。

## 1. 開始時点

Vulkan 版のリポジトリは空の状態から開始しました。

まず CUDA 版リポジトリの CPU 版コミットを基準として取り込みました。

```text
09d620824c0ed026242d914746727c6e5c5fcb93
```

取り込んだ主なファイル:

- `CMakeLists.txt`
- `README.md`
- `src/main.cpp`
- `src/camera.h`
- `src/vec3.h`
- `src/ray.h`
- `src/hittable.h`
- `src/sphere.h`
- `src/hittable_list.h`
- `src/material.h`

## 2. CPU 版の動作確認

取り込み確認として、いったん十分に軽い設定で CPU 版をレンダリングしました。

一時的な確認設定:

- 画像サイズ: 80 x 45
- サンプル数: 2 samples per pixel
- 最大反射回数: 5
- 合計 primary sample 数: 7,200

実行結果:

```text
Render settings:
  image: 80x45
  samples_per_pixel: 2
  max_depth: 5
  total primary samples: 7200

Render time: 0.756632 seconds
Pixels/sec: 4757.93
Primary samples/sec: 9515.86
```

確認後、設定は CPU 版の基準値へ戻しました。

```cpp
cam.image_width = 200;
cam.samples_per_pixel = 5;
cam.max_depth = 10;
```

## 3. Vulkan SDK と shader compiler

手元環境では Vulkan SDK が入っていました。

```text
VULKAN_SDK=C:\VulkanSDK\1.4.341.1
```

確認できたツール:

```text
glslangValidator.exe
vulkaninfo.exe
```

`glslangValidator` は GLSL shader を SPIR-V に変換するために使います。

今回の最初の実装では、Vulkan Ray Tracing Extension は使わず、Vulkan Compute Shader だけを使います。

## 4. 参考にした Vulkan サンプル

Vulkan 側の構成は、なるべく次のリポジトリに準拠する方針です。

```text
techmadot/vulkan-practical-guide-samples
```

特に参考にしたファイル:

- `Sources/CMakeLists.txt`
- `Sources/samples/cmake/CompileShaders.cmake`
- `Sources/samples/computeshader/CMakeLists.txt`

このサンプルでは、CMake 側で `glslangValidator` を呼び出して shader を SPIR-V に変換しています。

```cmake
COMMAND glslangValidator -V shader.comp --target-env vulkan1.3 -o shader.comp.spv
```

今回のプロジェクトでも、同じ考え方で `cmake/CompileShaders.cmake` を追加しました。

## 5. CMake の Vulkan 対応

`CMakeLists.txt` を更新し、Vulkan を optional にしました。

```cmake
option(USE_VULKAN "Enable Vulkan compute renderer support" ON)
```

Vulkan が有効な場合だけ、次を行います。

- `find_package(Vulkan REQUIRED)`
- `cmake/CompileShaders.cmake` の読み込み
- `shaders/gradient.comp` の SPIR-V 変換
- `src/vulkan_renderer.cpp` / `src/vulkan_renderer.h` の追加
- `RTWEEKEND_VULKAN_ENABLED` の定義
- `Vulkan::Vulkan` へのリンク

これにより、CPU 版を残したまま Vulkan 側の実装を追加できます。

## 6. shader compile 関数

`techmadot` サンプルの `CompileShaders.cmake` に寄せて、次の関数を追加しました。

```text
cmake/CompileShaders.cmake
```

主な仕様:

- 入力 shader に対して `.spv` を生成する。
- `--target-env vulkan1.3` を指定する。
- `OUTPUT_NAME` で出力名を変えられる。
- `DEFINES` で shader compile 時の `-D` を渡せる。

今回の shader:

```text
shaders/gradient.comp
```

生成される SPIR-V:

```text
shaders/gradient.comp.spv
```

`.spv` は生成物なので Git には含めないようにしています。

## 7. Vulkan renderer の追加

次のファイルを追加しました。

- `src/vulkan_renderer.h`
- `src/vulkan_renderer.cpp`

追加した関数:

```cpp
void render_vulkan_gradient(const char* output_file, int image_width, int image_height);
```

最初の段階では、`techmadot` サンプルのようなウィンドウ表示や swapchain は使っていません。

理由:

- 今回の目標は offscreen の compute 実行だけで十分。
- まずは buffer / descriptor / compute pipeline / dispatch / readback を確認したい。
- 生成結果は `.ppm` で保存できればよい。

## 8. Vulkan Compute Shader で背景グラデーションを生成

`shaders/gradient.comp` では、1 pixel を 1 invocation が担当します。

処理の流れ:

1. `gl_GlobalInvocationID` から pixel 座標を取得する。
2. push constant で渡された `width` / `height` を使って範囲外を除外する。
3. y 座標から白と水色を線形補間する。
4. RGB を 1 個の `uint` に詰めて storage buffer に書き込む。

shader 側の出力 buffer:

```glsl
layout(std430, binding = 0) buffer Pixels {
    uint pixels[];
};
```

CPU 側では、Vulkan の host visible / host coherent memory に出力 buffer を作り、dispatch 後に map して PPM として書き出します。

出力先:

```text
image_vulkan_gradient.ppm
```

## 9. 実行方法

構成:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B build_nmake -G "NMake Makefiles"'
```

ビルド:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake --build build_nmake'
```

Vulkan Compute Shader 版の実行:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && build_nmake\inOneWeekend.exe --renderer vulkan'
```

CPU 版は引数なしで実行します。

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && build_nmake\inOneWeekend.exe'
```

## 10. 確認結果

CMake 構成時に Vulkan SDK が検出されました。

```text
-- Found Vulkan: C:/VulkanSDK/1.4.341.1/Lib/vulkan-1.lib (found version "1.4.341") found components: glslc glslangValidator
```

ビルド時に shader compile が実行されました。

```text
[ 25%] Compiling shader: gradient.comp -> C:/Users/iynoz/Documents/ray_tracing_in_one_weekend/ProjectRoot_Vulkan/shaders/gradient.comp.spv
[100%] Built target inOneWeekend
```

Vulkan 版の実行結果:

```text
Vulkan compute gradient written to image_vulkan_gradient.ppm
  image: 200x112
```

PPM の先頭は白、末尾は水色に近い値になりました。

```text
P3
200 112
255
255 255 255
```

末尾付近:

```text
128 179 255
```

この時点で、Vulkan Compute Shader による最小の GPU 計算、storage buffer への書き込み、CPU への読み戻し、PPM 保存まで確認できました。

## 11. Vulkan Compute Shader で球 1 個との交差判定

背景グラデーションの次の段階として、Vulkan Compute Shader 内で ray を作り、球 1 個との交差判定を行う処理を追加しました。

最初は CPU 版の `sphere` / `hittable` / `material` をそのまま shader に持ち込まず、Vulkan shader 側で扱いやすい小さな構造にします。

追加した shader:

```text
shaders/sphere.comp
```

出力先:

```text
image_vulkan_sphere.ppm
```

処理の流れ:

1. shader 内で camera ray を作る。
2. 固定の球 1 個との交差判定を行う。
3. hit した場合は法線カラーで描画する。
4. miss した場合は今回と同じ背景グラデーションを描画する。
5. storage buffer に RGB を詰めた `uint` として書き込む。
6. CPU 側で buffer を map し、PPM として保存する。

Vulkan 版の実行結果:

```text
Vulkan compute single sphere written to image_vulkan_sphere.ppm
  image: 200x112
```

この時点で、CUDA 版の `GPU で球 1 個との交差判定を行う` 段階と対応するところまで進みました。

## 12. 複数の球を GPU buffer で扱う

球 1 個の交差判定が動いたので、次に複数の球を GPU buffer で扱えるようにしました。

追加した shader:

```text
shaders/spheres.comp
```

出力先:

```text
image_vulkan_spheres.ppm
```

CPU 側では、次の形の球データを `std::vector` で用意します。

```cpp
struct GpuSphere {
    float center_x;
    float center_y;
    float center_z;
    float radius;
};
```

Vulkan 側では、この配列を storage buffer として作成し、descriptor の binding 1 へ渡します。

descriptor の使い分け:

- binding 0: 出力 pixel buffer
- binding 1: 球配列 buffer

現在の確認用シーンでは、次の 4 個の球を使っています。

- 地面用の巨大な球
- 中央の球
- 左の球
- 右の球

処理の流れ:

1. CPU 側で球の配列を用意する。
2. Vulkan の storage buffer に球データを入れる。
3. shader 側で全ての球との交差判定を行う。
4. 一番近い hit を採用する。
5. hit した場合は法線カラーで描画する。
6. miss した場合は背景グラデーションを描画する。
7. `image_vulkan_spheres.ppm` として保存する。

Vulkan 版の実行結果:

```text
Vulkan compute multiple spheres written to image_vulkan_spheres.ppm
  image: 200x112
  spheres: 4
```

この段階で、CUDA 版の `GPU で複数の球との交差判定` に対応するところまで進みました。

### 補足: storage buffer にした意味

球 1 個の段階では、球データは shader に直接書いていました。

```glsl
float t = hit_sphere(vec3(0.0, 0.0, -1.0), 0.5, ray_origin, ray_direction);
```

つまり、CPU 側から球データを渡していたわけではなく、shader の中に固定の球が 1 個だけ埋め込まれていました。

複数球の段階では、CPU 側で球配列を作り、それを Vulkan の storage buffer に入れて shader の `binding = 1` に渡します。

```glsl
layout(std430, binding = 1) readonly buffer Spheres {
    Sphere spheres[];
};
```

各 pixel を担当する compute shader invocation は、この `spheres[]` を順番に調べて、一番近い hit を採用します。

この変更は、球 1 個だけを描く処理を速くするためというより、CPU 側で作った任意個数の球を GPU 側で扱えるようにするためのものです。

球 1 個だけなら shader にハードコードした方が単純ですが、複数球やランダムシーンに進むには storage buffer でシーンデータを渡す形が必要になります。

## 13. Lambertian 風の diffuse bounce を追加する

複数球の交差判定が動いたので、次に Lambertian 風の diffuse bounce を追加しました。

追加した shader:

```text
shaders/diffuse.comp
```

出力先:

```text
image_vulkan_diffuse.ppm
```

この段階ではまだ CPU 版の `material` クラスは移植していません。
まずは全ての球を同じ Lambertian 風の拡散面として扱います。

処理の流れ:

1. 各 pixel の compute shader invocation が camera ray を作る。
2. storage buffer の球配列を全て調べ、一番近い hit を探す。
3. hit した場合、hit point の法線にランダムな単位球内方向を足して bounce 方向を作る。
4. その bounce 方向の背景色を取り、`0.5` 倍して diffuse 反射っぽく暗くする。
5. miss した場合は、これまで通り背景グラデーションを描画する。

擬似乱数は pixel 座標から作った seed を hash して生成しています。

```glsl
uint rng_state = hash_u32((y * pc.width + x) ^ 0x9e3779b9u);
```

このため、実行ごとに結果は安定しますが、この時点では 1 pixel 1 sample なのでざらつきは残ります。

Vulkan 版の実行結果:

```text
Vulkan compute diffuse spheres written to image_vulkan_diffuse.ppm
  image: 200x112
  spheres: 4
```

この段階で、CUDA 版の `GPU 側に簡単なマテリアル処理を追加する` に近い入口まで進みました。

## 14. ランダムサンプリングと複数 bounce を Compute Shader 側に移す

Lambertian 風の diffuse bounce が 1 回だけ動いたので、次に 1 pixel あたり複数 sample と複数 bounce を Compute Shader 側に移しました。

設定:

```text
samples_per_pixel = 10
max_depth = 5
```

この設定は C++ 側から push constant で shader に渡します。

```cpp
struct PushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t sphere_count;
    uint32_t samples_per_pixel;
    uint32_t max_depth;
};
```

shader 側では、各 pixel の invocation 内で次の処理を行います。

1. 1 pixel あたり `samples_per_pixel` 回の camera ray を作る。
2. 各 ray で最大 `max_depth` 回まで bounce する。
3. hit したら Lambertian 風に `normal + random_in_unit_sphere()` 方向へ進める。
4. bounce ごとに attenuation を `0.5` 倍する。
5. miss したら現在の attenuation と背景色を掛けて返す。
6. 全 sample の平均を取り、最後に簡単な gamma 補正として `sqrt(color)` をかける。

Vulkan 版の実行結果:

```text
Vulkan compute diffuse spheres written to image_vulkan_diffuse.ppm
  image: 200x112
  spheres: 4
  samples_per_pixel: 10
  max_depth: 5
```

この段階で、CUDA 版の `ランダムサンプリングと複数 bounce を GPU 側に移す` に対応するところまで進みました。

### 補足: ランダムサンプリングと複数 bounce の意味

ランダムサンプリングは、1 pixel につき ray を 1 本だけ飛ばすのではなく、pixel 内の少し違う位置から複数本飛ばして平均することです。

今回の設定では、1 pixel あたり 10 本の ray を飛ばしています。

```text
samples_per_pixel = 10
```

これにより、ジャギーや diffuse 反射のノイズを平均化できます。

複数 bounce は、ray が物体に当たったあと、そこで止めずに反射方向へさらに ray を飛ばすことです。

```text
camera ray
  -> sphere に hit
  -> diffuse 方向へ bounce
  -> また hit 判定
  -> ...
```

今回の設定では、最大 5 回まで bounce します。

```text
max_depth = 5
```

つまり今回の shader では、各 pixel で 10 本の ray を飛ばし、それぞれ最大 5 回まで bounce させて、最後に平均しています。

## 15. metal / dielectric を追加する

ランダムサンプリングと複数 bounce が動いたので、次に `metal` / `dielectric` を追加しました。

追加した shader:

```text
shaders/materials.comp
```

出力先:

```text
image_vulkan_materials.ppm
```

球データは、material 情報も持てるように拡張しました。

```cpp
struct GpuSphere {
    float center_x;
    float center_y;
    float center_z;
    float radius;
    float albedo_r;
    float albedo_g;
    float albedo_b;
    float material_type;
    float fuzz;
    float refraction_index;
    float padding0;
    float padding1;
};
```

shader 側では、std430 の stride を揃えるために 3 つの `vec4` として受け取ります。

```glsl
struct Sphere {
    vec4 center_radius;
    vec4 albedo_type;
    vec4 params;
};
```

material type:

- `0`: Lambertian
- `1`: Metal
- `2`: Dielectric

現在の確認用シーン:

- 地面: Lambertian
- 中央の球: Lambertian
- 左の球: Dielectric
- 右の球: Metal

Metal は `reflect()` と fuzz を使って散乱方向を作ります。

Dielectric は、表裏で屈折率比を切り替え、全反射または Schlick 近似の反射率に応じて `reflect()` / `refract()` を選びます。

設定:

```text
samples_per_pixel = 20
max_depth = 10
```

Vulkan 版の実行結果:

```text
Vulkan compute material spheres written to image_vulkan_materials.ppm
  image: 200x112
  spheres: 4
  samples_per_pixel: 20
  max_depth: 10
```

この段階で、CUDA 版の `metal` / `dielectric` 追加に対応するところまで進みました。

## 16. カメラ設定、defocus blur、ランダムシーン生成を移植する

CPU 版の最終シーンに近づけるため、カメラ設定、defocus blur、ランダムシーン生成を Vulkan 版へ移しました。

CPU 側で、`Ray Tracing in One Weekend` の最終シーンに近い球配列を決定論的に生成し、storage buffer として shader へ渡します。

現在の球数:

```text
spheres = 485
```

カメラ設定:

```text
lookfrom = (13, 2, 3)
lookat = (0, 0, 0)
vup = (0, 1, 0)
vfov = 20
focus_dist = 10
defocus_angle = 0.6
```

確認用のレンダリング設定:

```text
image = 200x112
samples_per_pixel = 10
max_depth = 10
```

CPU 側で camera basis と pixel delta を計算し、push constant で shader へ渡します。

```cpp
camera_center
pixel00_loc
pixel_delta_u
pixel_delta_v
defocus_disk_u
defocus_disk_v
```

shader 側では、各 sample ごとに defocus disk 内のランダム点から ray を飛ばします。

```glsl
vec2 disk_sample = random_in_unit_disk(rng_state);
ray_origin += disk_sample.x * pc.defocus_disk_u.xyz
           +  disk_sample.y * pc.defocus_disk_v.xyz;
```

Vulkan 版の実行結果:

```text
Vulkan compute material spheres written to image_vulkan_materials.ppm
  image: 200x112
  spheres: 485
  samples_per_pixel: 10
  max_depth: 10
```

この段階で、CUDA 版の `カメラ設定、defocus blur、ランダムな多数の球を GPU 側で扱う` に対応するところまで進みました。

## 17. 次にやること

次は CPU 版 / CUDA 版 / Vulkan 版のレンダリング時間を比較する段階へ進みます。
